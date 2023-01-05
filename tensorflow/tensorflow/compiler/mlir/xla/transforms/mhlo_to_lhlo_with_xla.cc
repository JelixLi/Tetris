/* Copyright 2020 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/compiler/mlir/xla/transforms/mhlo_to_lhlo_with_xla.h"

#include <memory>
#include <tuple>

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"  // from @llvm-project
#include "mlir/IR/AffineExpr.h"  // from @llvm-project
#include "mlir/IR/AffineMap.h"  // from @llvm-project
#include "mlir/IR/Attributes.h"  // from @llvm-project
#include "mlir/IR/Builders.h"  // from @llvm-project
#include "mlir/IR/Dialect.h"  // from @llvm-project
#include "mlir/IR/Location.h"  // from @llvm-project
#include "mlir/IR/MLIRContext.h"  // from @llvm-project
#include "mlir/IR/Module.h"  // from @llvm-project
#include "mlir/IR/OpDefinition.h"  // from @llvm-project
#include "mlir/IR/Operation.h"  // from @llvm-project
#include "mlir/IR/PatternMatch.h"  // from @llvm-project
#include "mlir/IR/StandardTypes.h"  // from @llvm-project
#include "mlir/IR/SymbolTable.h"  // from @llvm-project
#include "mlir/Pass/Pass.h"  // from @llvm-project
#include "mlir/Pass/PassOptions.h"  // from @llvm-project
#include "mlir/Translation.h"  // from @llvm-project
#include "tensorflow/compiler/mlir/hlo/include/mlir-hlo/Dialect/mhlo/IR/hlo_ops.h"
#include "tensorflow/compiler/mlir/hlo/include/mlir-hlo/Dialect/mhlo/IR/lhlo_ops.h"
#include "tensorflow/compiler/mlir/xla/hlo_function_importer.h"
#include "tensorflow/compiler/mlir/xla/hlo_utils.h"
#include "tensorflow/compiler/mlir/xla/mlir_hlo_to_hlo.h"
#include "tensorflow/compiler/mlir/xla/xla_mlir_translate_cl.h"
#include "tensorflow/compiler/xla/debug_options_flags.h"
#include "tensorflow/compiler/xla/service/backend.h"
#include "tensorflow/compiler/xla/service/buffer_assignment.h"
#include "tensorflow/compiler/xla/service/hlo_casting_utils.h"
#include "tensorflow/compiler/xla/service/hlo_computation.h"
#include "tensorflow/compiler/xla/service/hlo_instruction.h"
#include "tensorflow/compiler/xla/service/hlo_instructions.h"
#include "tensorflow/compiler/xla/service/hlo_module.h"
#include "tensorflow/compiler/xla/service/hlo_parser.h"
#include "tensorflow/compiler/xla/statusor.h"
#include "tensorflow/compiler/xla/util.h"

using xla::BufferAllocation;
using xla::BufferAssignment;
using xla::HloComputation;
using xla::HloInstruction;
using xla::HloModule;
using xla::HloModuleProto;
using xla::HloProto;
using xla::Shape;
using xla::StatusOr;

namespace mlir {
namespace {

absl::string_view StringRefToView(llvm::StringRef ref) {
  return {ref.data(), ref.size()};
}

StatusOr<std::unique_ptr<HloModule>> HloModuleFromProto(
    const HloProto& hlo_proto) {
  const HloModuleProto& module_proto = hlo_proto.hlo_module();
  TF_ASSIGN_OR_RETURN(const ::xla::HloModuleConfig module_config,
                      HloModule::CreateModuleConfigFromProto(
                          module_proto, ::xla::GetDebugOptionsFromFlags()));
  return HloModule::CreateFromProto(module_proto, module_config);
}

// Convert the MLIR `module` from HLO dialect to LHLO dialect using XLA for the
// given platform.
Status ConvertModule(std::unique_ptr<HloModule> hlo_module, ModuleOp module,
                     StringRef platform_name) {
  auto platform = ::xla::se::MultiPlatformManager::PlatformWithName(
      StringRefToView(platform_name));
  if (!platform.ok()) {
    std::string error_msg;
    llvm::raw_string_ostream os(error_msg);
    os << "failed to get platform: " << platform.status().ToString()
       << " (available Platform: ";
    std::vector<std::string> available_platforms;
    (void)::xla::se::MultiPlatformManager::PlatformsWithFilter(
        [&](const stream_executor::Platform* p) {
          available_platforms.push_back(p->Name());
          return false;
        });
    llvm::interleaveComma(available_platforms, os);
    os << ")";
    return ::xla::InvalidArgument("%s", os.str().c_str());
  }

  ::xla::BackendOptions backend_options;
  backend_options.set_platform(platform.ValueOrDie());
  auto backend_or_err = ::xla::Backend::CreateBackend(backend_options);
  TF_RETURN_WITH_CONTEXT_IF_ERROR(backend_or_err.status(),
                                  "failed to create XLA Backend ");
  auto backend = std::move(backend_or_err.ValueOrDie());

  // Run all HLO passes to produce an optimized module.
  auto result_or = backend->compiler()->RunHloPassesAndBufferAssignement(
      std::move(hlo_module), backend->default_stream_executor(),
      backend->memory_allocator(), optimize_xla_hlo);
  TF_RETURN_WITH_CONTEXT_IF_ERROR(result_or.status(),
                                  "running XLA pass pipeline");
  std::unique_ptr<HloModule> optimized_hlo_module =
      std::move(std::get<0>(result_or.ValueOrDie()));
  std::unique_ptr<BufferAssignment> assignment =
      std::move(std::get<1>(result_or.ValueOrDie()));

  // Clear the module before populating it back with the result of the
  // conversion.
  module.getBody()->clear();
  OpBuilder builder(module);
  module.ensureTerminator(module.getBodyRegion(), builder, module.getLoc());

  TF_RETURN_WITH_CONTEXT_IF_ERROR(
      HloToLhloModule(*assignment, *optimized_hlo_module, module),
      "converting HLO to LHLO");

  return Status::OK();
}

// This pass takes an MLIR HLO module, converts it to XLA to perform the HLO
// optimization pipeline for the required platform, and then converts it back to
// MLIR LHLO.
class XlaHloToLhloPass
    : public PassWrapper<XlaHloToLhloPass, OperationPass<ModuleOp>> {
  void getDependentDialects(DialectRegistry& registry) const override {
    registry.insert<mlir::StandardOpsDialect, mlir::mhlo::MhloDialect,
                    mlir::lmhlo::LmhloDialect>();
  }

 public:
  XlaHloToLhloPass() = default;
  XlaHloToLhloPass(const XlaHloToLhloPass&) {}

 private:
  void runOnOperation() final {
    ModuleOp module = getOperation();

    auto status = [&module, this]() -> Status {
      SymbolTable symbol_table(module);
      if (!symbol_table.lookup("main")) {
        return ::xla::InvalidArgument(
            "conversion to HLO module failed: missing main()");
      }
      HloProto hlo_proto;
      TF_RETURN_WITH_CONTEXT_IF_ERROR(
          ConvertMlirHloToHlo(module, &hlo_proto,
                              /*use_tuple_args=*/false,
                              /*return_tuple=*/false,
                              /*shape_representation_fn=*/nullptr),
          "conversion to XLA HLO proto failed");

      auto statusOrHloModule = HloModuleFromProto(hlo_proto);
      TF_RETURN_WITH_CONTEXT_IF_ERROR(statusOrHloModule.status(),
                                      "parsing HLO proto to HLO module failed");
      std::unique_ptr<HloModule> hlo_module =
          std::move(statusOrHloModule.ValueOrDie());

      return ConvertModule(std::move(hlo_module), module, platform_);
    }();
    if (!status.ok()) {
      module.emitError() << status.ToString();
      return signalPassFailure();
    }
  }

  Option<std::string> platform_{
      *this, "platform",
      llvm::cl::desc("The platform to use for the XLA optimization pipeline."),
      llvm::cl::init("Host")};
};

}  // namespace

template <typename OpType>
StatusOr<OpType> LhloDialectEmitter::CreateOpWithoutAttrs(
    HloInstruction* instr) {
  Location loc = getLocation(instr);
  std::pair<Identifier, Attribute> attrs[] = {
      {Identifier::get("name", builder_.getContext()),
       builder_.getStringAttr(instr->name())},
  };
  ArrayRef<Type> rets{};

  llvm::SmallVector<Value, 4> operands;
  for (const HloInstruction* operand : instr->operands()) {
    TF_RETURN_IF_ERROR(GetOrCreateView(operand, &operands));
  }
  TF_RETURN_IF_ERROR(GetOrCreateView(instr, &operands));

  return builder_.create<OpType>(loc, rets, operands, attrs);
}

Status LhloDialectEmitter::DefaultAction(HloInstruction* instr) {
  using ::xla::HloOpcode;
  switch (instr->opcode()) {
    case HloOpcode::kAbs:
      return CreateOpWithoutAttrs<lmhlo::AbsOp>(instr).status();
    case HloOpcode::kAdd:
      return CreateOpWithoutAttrs<lmhlo::AddOp>(instr).status();
    case HloOpcode::kAnd:
      return CreateOpWithoutAttrs<lmhlo::AndOp>(instr).status();
    case HloOpcode::kCeil:
      return CreateOpWithoutAttrs<lmhlo::CeilOp>(instr).status();
    case HloOpcode::kComplex:
      return CreateOpWithoutAttrs<lmhlo::ComplexOp>(instr).status();
    case HloOpcode::kCopy:
      return CreateOpWithoutAttrs<lmhlo::CopyOp>(instr).status();
    case HloOpcode::kCos:
      return CreateOpWithoutAttrs<lmhlo::CosOp>(instr).status();
    case HloOpcode::kDivide:
      return CreateOpWithoutAttrs<lmhlo::DivOp>(instr).status();
    case HloOpcode::kExp:
      return CreateOpWithoutAttrs<lmhlo::ExpOp>(instr).status();
    case HloOpcode::kImag:
      return CreateOpWithoutAttrs<lmhlo::ImagOp>(instr).status();
    case HloOpcode::kLog:
      return CreateOpWithoutAttrs<lmhlo::LogOp>(instr).status();
    case HloOpcode::kMaximum:
      return CreateOpWithoutAttrs<lmhlo::MaxOp>(instr).status();
    case HloOpcode::kMinimum:
      return CreateOpWithoutAttrs<lmhlo::MinOp>(instr).status();
    case HloOpcode::kMultiply:
      return CreateOpWithoutAttrs<lmhlo::MulOp>(instr).status();
    case HloOpcode::kNegate:
      return CreateOpWithoutAttrs<lmhlo::NegOp>(instr).status();
    case HloOpcode::kReal:
      return CreateOpWithoutAttrs<lmhlo::RealOp>(instr).status();
    case HloOpcode::kRemainder:
      return CreateOpWithoutAttrs<lmhlo::RemOp>(instr).status();
    case HloOpcode::kRsqrt:
      return CreateOpWithoutAttrs<lmhlo::RsqrtOp>(instr).status();
    case HloOpcode::kSelect:
      return CreateOpWithoutAttrs<lmhlo::SelectOp>(instr).status();
    case HloOpcode::kSign:
      return CreateOpWithoutAttrs<lmhlo::SignOp>(instr).status();
    case HloOpcode::kSqrt:
      return CreateOpWithoutAttrs<lmhlo::SqrtOp>(instr).status();
    case HloOpcode::kSubtract:
      return CreateOpWithoutAttrs<lmhlo::SubOp>(instr).status();
    case HloOpcode::kTanh:
      return CreateOpWithoutAttrs<lmhlo::TanhOp>(instr).status();
    default:
      llvm::errs() << instr->ToString();
      return tensorflow::errors::Internal(
          absl::StrCat("LHLO opcode ", ::xla::HloOpcodeString(instr->opcode()),
                       " is not supported."));
  }
  return Status::OK();
}

StatusOr<lmhlo::SortOp> LhloDialectEmitter::EmitSortOp(HloInstruction* instr) {
  TF_ASSIGN_OR_RETURN(auto sort, CreateOpWithoutAttrs<lmhlo::SortOp>(instr));
  auto* sort_instr = ::xla::Cast<::xla::HloSortInstruction>(instr);
  sort.dimensionAttr(builder_.getI64IntegerAttr(sort_instr->sort_dimension()));
  sort.is_stableAttr(builder_.getBoolAttr(sort_instr->is_stable()));
  TF_RETURN_IF_ERROR(::xla::HloFunctionImporter::ImportAsRegion(
      *sort_instr->called_computations()[0], &sort.comparator(), &builder_));
  return sort;
}

Status LhloDialectEmitter::HandleSort(HloInstruction* instr) {
  return EmitSortOp(instr).status();
}

// Walks MHLO::TupleOp recursively.
Status WalkTuplePostOrder(Value v,
                          const std::function<Status(Value)>& visitor) {
  if (auto* op = v.getDefiningOp()) {
    if (auto tuple = dyn_cast<mhlo::TupleOp>(op)) {
      for (Value sub_v : tuple.val()) {
        TF_RETURN_IF_ERROR(WalkTuplePostOrder(sub_v, visitor));
      }
      return Status::OK();
    }
  }
  return visitor(v);
}

// This function removes all uses of a fused region argument, and rewire those
// uses to a `tensor_load %memref`, where %memref is caller argument.
//
// It also flattens all input/output tuples into more region arguments /
// results.
StatusOr<Value> LhloDialectEmitter::RewriteFusionOperand(
    const HloInstruction* root, const Shape& shape,
    ::xla::ShapeIndex* shape_index, OpBuilder* b, Location loc) {
  if (shape.IsTuple()) {
    llvm::SmallVector<Value, 4> values;
    for (int i = 0; i < shape.tuple_shapes_size(); i++) {
      shape_index->push_back(i);
      TF_ASSIGN_OR_RETURN(
          auto v, RewriteFusionOperand(root, shape.tuple_shapes(i), shape_index,
                                       b, loc));
      values.push_back(v);
      shape_index->pop_back();
    }
    return Value(b->create<mhlo::TupleOp>(loc, values));
  }
  TF_ASSIGN_OR_RETURN(Value memref,
                      GetOrCreateArrayView(root, shape, *shape_index));
  auto load = b->create<TensorLoadOp>(loc, memref);
  if (shape.layout() !=
      xla::LayoutUtil::MakeDescendingLayout(shape.dimensions().size())) {
    llvm::SmallVector<int64_t, 4> minor_to_major(
        shape.layout().minor_to_major().begin(),
        shape.layout().minor_to_major().end());
    load.setAttr("minor_to_major", b->getIndexTensorAttr(minor_to_major));
  }
  return load.getResult();
}

StatusOr<lmhlo::FusionOp> LhloDialectEmitter::EmitFusionOp(
    HloInstruction* instr) {
  Location loc = getLocation(instr);

  auto* fusion_instr = ::xla::Cast<::xla::HloFusionInstruction>(instr);

  auto fusion = builder_.create<lmhlo::FusionOp>(getLocation(instr),
                                                 ArrayRef<NamedAttribute>{});
  auto after_fusion = builder_.saveInsertionPoint();
  builder_ = mlir::OpBuilder(fusion);

  auto region_builder = OpBuilder::atBlockBegin(&fusion.region().front());

  llvm::SmallVector<Value, 8> arguments;
  for (int i = 0; i < instr->operands().size(); i++) {
    const HloInstruction* operand = instr->operand(i);
    xla::ShapeIndex shape_index;
    TF_ASSIGN_OR_RETURN(
        auto arg, RewriteFusionOperand(operand, operand->shape(), &shape_index,
                                       &region_builder, loc));
    arguments.push_back(arg);
  }

  TF_ASSIGN_OR_RETURN(Value result,
                      ::xla::HloFunctionImporter::ImportInstructions(
                          *fusion_instr->fused_instructions_computation(),
                          arguments, &region_builder));

  {
    int i = 0;
    llvm::SmallVector<Value, 4> output;
    TF_RETURN_IF_ERROR(GetOrCreateView(instr, &output));
    TF_RETURN_IF_ERROR(WalkTuplePostOrder(result, [&](Value v) mutable {
      region_builder.create<TensorStoreOp>(loc, v, output[i++]);
      return Status::OK();
    }));
    if (i != output.size()) {
      return ::xla::InternalError("output sizes don't match");
    }
  }

  // Fold GTE/Tuple pairs.
  //
  // Since the fused region refers to values in its parent region, we can't
  // call applyPatternAndFoldGreedily. We optimize it manually.
  //
  // Only walk once, because post-ordering is exactly what we need for GTE
  // optimizations.
  fusion.region().walk([](mhlo::GetTupleElementOp gte) {
    SmallVector<Value, 4> folded_values;
    if (succeeded(OpBuilder(gte).tryFold(gte, folded_values))) {
      gte.replaceAllUsesWith(folded_values[0]);
    }
  });

  // Effectively a DCE on the region.
  {
    llvm::SmallVector<mlir::Operation*, 4> ops;
    fusion.region().walk([&](mlir::Operation* op) { ops.push_back(op); });
    // Visit the user first.
    std::reverse(ops.begin(), ops.end());
    for (auto op : ops) {
      if (isOpTriviallyDead(op)) op->erase();
    }
  }

  builder_.restoreInsertionPoint(after_fusion);
  return fusion;
}

Status LhloDialectEmitter::HandleFusion(HloInstruction* instr) {
  return EmitFusionOp(instr).status();
}

StatusOr<mhlo::ScatterDimensionNumbers>
LhloDialectEmitter::GetScatterDimensionNumbers(HloInstruction* instr) {
  auto* scatter_instr = ::xla::Cast<::xla::HloScatterInstruction>(instr);

  const ::xla::ScatterDimensionNumbers& xla_scatter_dim =
      scatter_instr->scatter_dimension_numbers();
  auto scatter_dimension_numbers = mhlo::ScatterDimensionNumbers::get(
      getI64DenseElementsAttr(xla_scatter_dim.update_window_dims()),
      getI64DenseElementsAttr(xla_scatter_dim.inserted_window_dims()),
      getI64DenseElementsAttr(xla_scatter_dim.scatter_dims_to_operand_dims()),
      builder_.getI64IntegerAttr(xla_scatter_dim.index_vector_dim()),
      module_.getContext());
  return scatter_dimension_numbers;
}

StatusOr<lmhlo::ScatterOp> LhloDialectEmitter::EmitScatterOp(
    HloInstruction* instr) {
  TF_ASSIGN_OR_RETURN(auto scatter,
                      CreateOpWithoutAttrs<lmhlo::ScatterOp>(instr));

  // copy attributes
  auto* scatter_instr = ::xla::Cast<::xla::HloScatterInstruction>(instr);

  TF_ASSIGN_OR_RETURN(auto scatter_dimension_numbers,
                      GetScatterDimensionNumbers(instr));
  scatter.scatter_dimension_numbersAttr(scatter_dimension_numbers);
  scatter.indices_are_sortedAttr(
      builder_.getBoolAttr(scatter_instr->indices_are_sorted()));
  scatter.unique_indicesAttr(
      builder_.getBoolAttr(scatter_instr->unique_indices()));

  // import update computation as region
  TF_RETURN_IF_ERROR(::xla::HloFunctionImporter::ImportAsRegion(
      *scatter_instr->called_computations()[0], &scatter.update_computation(),
      &builder_));

  return scatter;
}

Status LhloDialectEmitter::HandleScatter(HloInstruction* instr) {
  return EmitScatterOp(instr).status();
}

StatusOr<Value> LhloDialectEmitter::GetOrCreateArrayView(
    const ::xla::HloInstruction* instr, const ::xla::Shape& current_shape,
    const ::xla::ShapeIndex& shape_index) {
  TF_ASSIGN_OR_RETURN(Type out_type, ::xla::ConvertShapeToType<MemRefType>(
                                         current_shape, builder_));
  TF_ASSIGN_OR_RETURN(BufferAllocation::Slice slice,
                      assignment_.GetUniqueSlice(instr, shape_index));
  Value alloc = allocations_[slice.allocation()];
  if (alloc.getType() == out_type && slice.offset() == 0) {
    return alloc;
  }

  auto out_memref_type = out_type.dyn_cast<MemRefType>();
  if (!out_memref_type)
    return tensorflow::errors::Internal(
        "Expected memref type when creating a view for leaf type of a tuple.");

  // Cache generated ViewOp and StaticMemRefCastOp by (instruction,
  // shape_index).
  auto& cached_value = slices_[std::make_pair(instr, shape_index)];
  if (cached_value) {
    return cached_value;
  }

  Value byte_shift =
      builder_.create<ConstantIndexOp>(alloc.getLoc(), slice.offset());

  xla::Shape physical_shape =
      xla::ShapeUtil::MakeShapeWithDescendingLayoutAndSamePhysicalLayout(
          current_shape);
  TF_ASSIGN_OR_RETURN(
      Type physical_out_type,
      ::xla::ConvertShapeToType<MemRefType>(physical_shape, builder_));

  // TODO(timshen): revisit location handling.
  Location loc = builder_.getUnknownLoc();

  // ViewOp only takes memrefs without affine maps (layouts). Let ViewOp produce
  // the physical shape (where dimensions are ordered in major to minor) first,
  // then follow up with a StaticMemRefCastOp to cast the resulting memref to
  // the original layout.
  Value result =
      builder_.create<ViewOp>(loc, physical_out_type, alloc, byte_shift,
                              /*sizes=*/ValueRange{});
  if (physical_out_type != out_type)
    result = builder_.create<lmhlo::StaticMemRefCastOp>(loc, out_memref_type,
                                                        result);
  return cached_value = result;
}

Status LhloDialectEmitter::GetOrCreateViewImpl(
    const HloInstruction* instr, const Shape& current_shape,
    ::xla::ShapeIndex* current_shape_index, SmallVectorImpl<Value>* values) {
  if (current_shape.IsTuple()) {
    for (int i = 0; i < current_shape.tuple_shapes().size(); i++) {
      current_shape_index->push_back(i);
      TF_RETURN_IF_ERROR(GetOrCreateViewImpl(
          instr, current_shape.tuple_shapes(i), current_shape_index, values));
      current_shape_index->pop_back();
    }
    return Status::OK();
  }
  TF_ASSIGN_OR_RETURN(
      auto v, GetOrCreateArrayView(instr, current_shape, *current_shape_index));
  values->push_back(v);
  return Status::OK();
}

// Returns a view for the result of an instruction.
// We first get a view for the slice in the allocation, and then may need to
// create another view to adjust the slice for the shape of the instruction.
Status LhloDialectEmitter::GetOrCreateView(const HloInstruction* instr,
                                           SmallVectorImpl<Value>* values) {
  ::xla::ShapeIndex shape_index;
  return GetOrCreateViewImpl(instr, instr->shape(), &shape_index, values);
}

Status LhloDialectEmitter::Initialize() {
  std::string function_name =
      computation_.name().empty() ? "__compute" : computation_.name();

  // Create the function as () -> (), we'll compute the arguments from the
  // buffer allocation and update the type then.
  auto func_op = FuncOp::create(builder_.getUnknownLoc(), function_name,
                                builder_.getFunctionType({}, {}));
  Block* block = func_op.addEntryBlock();

  llvm::SmallVector<const BufferAllocation*, 8> ordered_allocations;
  for (const BufferAllocation& alloc : assignment_.Allocations())
    ordered_allocations.push_back(&alloc);

  if (computation_.IsEntryComputation()) {
    // Sort the rather arbitrarily ordered allocations to match the input/output
    // parameters. Specifically we want to sort buffer allocations in the
    // following order:
    // * Parameters always order before non-parameters.
    // * Different parameters order by parameter number.
    // * Different allocations for the same parameter order by the shape index.
    //
    // TODO(timshen): there should be only one non-parameter buffer, the temp
    // buffer. Check on that.
    const auto allocation_comparator = [](const BufferAllocation* lhs,
                                          const BufferAllocation* rhs) {
      if (lhs->is_entry_computation_parameter() !=
          rhs->is_entry_computation_parameter()) {
        return lhs->is_entry_computation_parameter() >
               rhs->is_entry_computation_parameter();
      }
      if (lhs->is_entry_computation_parameter()) {
        return std::tuple<int, const ::xla::ShapeIndex&>(
                   lhs->parameter_number(), lhs->param_shape_index()) <
               std::tuple<int, const ::xla::ShapeIndex&>(
                   rhs->parameter_number(), rhs->param_shape_index());
      }
      return false;
    };

    std::stable_sort(ordered_allocations.begin(), ordered_allocations.end(),
                     allocation_comparator);
  }

  // The function signature will be composed of:
  // - one memref for each of the parameters.
  // - one memref for each other buffer allocation.
  llvm::SmallVector<MutableDictionaryAttr, 8> args_attrs;
  for (const BufferAllocation* alloc : ordered_allocations) {
    if (computation_.IsEntryComputation() &&
        alloc->is_entry_computation_parameter()) {
      const ::xla::Shape& buffer_shape = ::xla::ShapeUtil::GetSubshape(
          computation_.parameter_instruction(alloc->parameter_number())
              ->shape(),
          alloc->param_shape_index());

      TF_ASSIGN_OR_RETURN(auto arg_type, ::xla::ConvertShapeToType<MemRefType>(
                                             buffer_shape, builder_));

      // First map parameters to memrefs on the operation.
      block->addArgument(arg_type);
      allocations_[alloc] = block->getArguments().back();
      args_attrs.emplace_back();
      args_attrs.back().set(builder_.getIdentifier("lmhlo.alloc"),
                            builder_.getIndexAttr(alloc->index()));
      args_attrs.back().set(builder_.getIdentifier("lmhlo.params"),
                            builder_.getIndexAttr(alloc->parameter_number()));
    } else {
      block->addArgument(MemRefType::get({alloc->size()}, i8_type_));
      allocations_[alloc] = block->getArguments().back();
      args_attrs.emplace_back();
      args_attrs.back().set(builder_.getIdentifier("lmhlo.alloc"),
                            builder_.getIndexAttr(alloc->index()));
      if (alloc->maybe_live_out())
        args_attrs.back().set(builder_.getIdentifier("lmhlo.liveout"),
                              builder_.getBoolAttr(true));
    }
  }

  FunctionType function_type =
      builder_.getFunctionType(block->getArgumentTypes(), {});
  func_op.setType(function_type);
  func_op.setAllArgAttrs(args_attrs);

  SymbolTable symbol_table(module_);
  symbol_table.insert(func_op);
  builder_.setInsertionPointToEnd(block);

  auto return_op = builder_.create<ReturnOp>(builder_.getUnknownLoc());
  builder_ = OpBuilder(return_op);

  return Status::OK();
}

std::unique_ptr<OperationPass<ModuleOp>> createXlaHloToLhloWithXlaPass() {
  return std::make_unique<XlaHloToLhloPass>();
}

Status HloToLhloModule(const BufferAssignment& assignment,
                       const HloModule& hlo_module, ModuleOp module) {
  module.getContext()
      ->loadDialect<StandardOpsDialect, mhlo::MhloDialect,
                    lmhlo::LmhloDialect>();
  HloComputation* computation = hlo_module.entry_computation();

  LhloDialectEmitter emitter(assignment, *computation, module);
  TF_RETURN_IF_ERROR(emitter.Initialize());

  const ::xla::HloInstructionSequence* schedule =
      assignment.hlo_ordering().SequentialOrder(*computation);
  if (!schedule)
    return ::xla::Unimplemented("Missing sequential order for the computation");
  const std::vector<HloInstruction*>& ordering = schedule->instructions();
  return computation->AcceptOrdered(&emitter, ordering);
}

OwningModuleRef HloTextToLhloTranslateFunction(llvm::StringRef input,
                                               MLIRContext* context) {
  StatusOr<std::unique_ptr<HloModule>> maybe_module =
      xla::ParseAndReturnUnverifiedModule(
          absl::string_view(input.data(), input.size()));
  TF_CHECK_OK(maybe_module.status());

  OwningModuleRef module = ModuleOp::create(UnknownLoc::get(context));

  TF_CHECK_OK(
      ConvertModule(maybe_module.ConsumeValueOrDie(), module.get(), "Host"));

  return module;
}

static PassRegistration<XlaHloToLhloPass> registration(
    "xla-hlo-to-lhlo-with-xla",
    "Emit LHLO from HLO using the existing XLA implementation");

}  // namespace mlir
