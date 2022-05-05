/* Copyright 2017 The TensorFlow Authors. All Rights Reserved.

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

#include "tensorflow/compiler/xla/service/gpu/gpu_compiler.h"

#include <stdlib.h>

#include <atomic>
#include <functional>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "mlir/IR/Module.h"  // from @llvm-project
#include "mlir/InitAllDialects.h"  // from @llvm-project
#include "tensorflow/compiler/xla/protobuf_util.h"
#include "tensorflow/compiler/xla/service/algebraic_simplifier.h"
#include "tensorflow/compiler/xla/service/all_reduce_combiner.h"
#include "tensorflow/compiler/xla/service/batchnorm_expander.h"
#include "tensorflow/compiler/xla/service/buffer_assignment.h"
#include "tensorflow/compiler/xla/service/call_inliner.h"
#include "tensorflow/compiler/xla/service/comparison_expander.h"
#include "tensorflow/compiler/xla/service/conditional_canonicalizer.h"
#include "tensorflow/compiler/xla/service/conditional_simplifier.h"
#include "tensorflow/compiler/xla/service/convolution_4d_expander.h"
#include "tensorflow/compiler/xla/service/dot_decomposer.h"
#include "tensorflow/compiler/xla/service/dump.h"
#include "tensorflow/compiler/xla/service/dynamic_index_splitter.h"
#include "tensorflow/compiler/xla/service/dynamic_padder.h"
#include "tensorflow/compiler/xla/service/flatten_call_graph.h"
#include "tensorflow/compiler/xla/service/gather_expander.h"
#include "tensorflow/compiler/xla/service/gpu/alias_passthrough_params.h"
#include "tensorflow/compiler/xla/service/gpu/cudnn_batchnorm_rewriter.h"
#include "tensorflow/compiler/xla/service/gpu/fusion_merger.h"
#include "tensorflow/compiler/xla/service/gpu/gemm_rewriter.h"
#include "tensorflow/compiler/xla/service/gpu/gpu_constants.h"
#include "tensorflow/compiler/xla/service/gpu/gpu_conv_algorithm_picker.h"
#include "tensorflow/compiler/xla/service/gpu/gpu_copy_insertion.h"
#include "tensorflow/compiler/xla/service/gpu/gpu_executable.h"
#include "tensorflow/compiler/xla/service/gpu/gpu_hlo_schedule.h"
#include "tensorflow/compiler/xla/service/gpu/gpu_layout_assignment.h"
#include "tensorflow/compiler/xla/service/gpu/gpu_sanitize_constant_names.h"
#include "tensorflow/compiler/xla/service/gpu/gpu_scatter_expander.h"
#include "tensorflow/compiler/xla/service/gpu/horizontal_input_fusion.h"
#include "tensorflow/compiler/xla/service/gpu/horizontal_loop_fusion.h"
#include "tensorflow/compiler/xla/service/gpu/instruction_fusion.h"
#include "tensorflow/compiler/xla/service/gpu/ir_emission_utils.h"
#include "tensorflow/compiler/xla/service/gpu/ir_emitter_context.h"
#include "tensorflow/compiler/xla/service/gpu/ir_emitter_unnested.h"
#include "tensorflow/compiler/xla/service/gpu/launch_dimensions.h"
#include "tensorflow/compiler/xla/service/gpu/llvm_gpu_backend/gpu_backend_lib.h"
#include "tensorflow/compiler/xla/service/gpu/multi_output_fusion.h"
#include "tensorflow/compiler/xla/service/gpu/reduction_degenerate_dim_remover.h"
#include "tensorflow/compiler/xla/service/gpu/reduction_dimension_grouper.h"
#include "tensorflow/compiler/xla/service/gpu/reduction_layout_normalizer.h"
#include "tensorflow/compiler/xla/service/gpu/reduction_splitter.h"
#include "tensorflow/compiler/xla/service/gpu/stream_assignment.h"
#include "tensorflow/compiler/xla/service/gpu/stream_executor_util.h"
#include "tensorflow/compiler/xla/service/gpu/target_constants.h"
#include "tensorflow/compiler/xla/service/gpu/thunk_schedule.h"
#include "tensorflow/compiler/xla/service/gpu/tree_reduction_rewriter.h"
#include "tensorflow/compiler/xla/service/gpu/variadic_op_splitter.h"
#include "tensorflow/compiler/xla/service/hlo_computation.h"
#include "tensorflow/compiler/xla/service/hlo_constant_folding.h"
#include "tensorflow/compiler/xla/service/hlo_cse.h"
#include "tensorflow/compiler/xla/service/hlo_dataflow_analysis.h"
#include "tensorflow/compiler/xla/service/hlo_dce.h"
#include "tensorflow/compiler/xla/service/hlo_element_type_converter.h"
#include "tensorflow/compiler/xla/service/hlo_instruction.h"
#include "tensorflow/compiler/xla/service/hlo_pass_fix.h"
#include "tensorflow/compiler/xla/service/hlo_pass_pipeline.h"
#include "tensorflow/compiler/xla/service/hlo_proto_util.h"
#include "tensorflow/compiler/xla/service/hlo_subcomputation_unification.h"
#include "tensorflow/compiler/xla/service/hlo_verifier.h"
#include "tensorflow/compiler/xla/service/llvm_ir/llvm_util.h"
#include "tensorflow/compiler/xla/service/logistic_expander.h"
#include "tensorflow/compiler/xla/service/qr_expander.h"
#include "tensorflow/compiler/xla/service/reshape_mover.h"
#include "tensorflow/compiler/xla/service/rng_bit_generator_expander.h"
#include "tensorflow/compiler/xla/service/rng_expander.h"
#include "tensorflow/compiler/xla/service/slice_sinker.h"
#include "tensorflow/compiler/xla/service/slow_operation_alarm.h"
#include "tensorflow/compiler/xla/service/sort_simplifier.h"
#include "tensorflow/compiler/xla/service/stable_sort_expander.h"
#include "tensorflow/compiler/xla/service/transpose_folding.h"
#include "tensorflow/compiler/xla/service/tuple_simplifier.h"
#include "tensorflow/compiler/xla/service/while_loop_constant_sinking.h"
#include "tensorflow/compiler/xla/service/while_loop_simplifier.h"
#include "tensorflow/compiler/xla/service/while_loop_trip_count_annotator.h"
#include "tensorflow/compiler/xla/service/zero_sized_hlo_elimination.h"
#include "tensorflow/compiler/xla/status_macros.h"
#include "tensorflow/compiler/xla/types.h"
#include "tensorflow/compiler/xla/util.h"
#include "tensorflow/core/lib/core/status.h"
#include "tensorflow/core/lib/gtl/cleanup.h"
#include "tensorflow/core/lib/io/path.h"
#include "tensorflow/core/platform/env.h"
#include "tensorflow/core/platform/logging.h"
#include "tensorflow/core/platform/regexp.h"
#include "tensorflow/core/platform/stream_executor_no_cuda.h"
#include "tensorflow/core/platform/subprocess.h"
#include "tensorflow/core/platform/tracing.h"
#include "tensorflow/core/profiler/lib/traceme.h"
#include "tensorflow/core/util/env_var.h"

namespace xla {
namespace gpu {

GpuCompiler::GpuCompiler(se::Platform::Id platform_id,
                         const char* target_triple, const char* data_layout)
    : platform_id_(platform_id),
      target_triple_(target_triple),
      data_layout_(data_layout),
      pointer_size_(llvm::DataLayout(data_layout)
                        .getPointerSize(0 /* default address space */)) {}

// Runs optimization passes on the given HLO module.
Status GpuCompiler::OptimizeHloModule(
    HloModule* hlo_module, se::StreamExecutor* stream_exec,
    se::DeviceMemoryAllocator* device_allocator) {
  {
    HloPassPipeline pipeline("optimization");
    pipeline.AddInvariantChecker<HloVerifier>(/*layout_sensitive=*/false,
                                              /*allow_mixed_precision=*/false);

    // Expand random number generation.
    pipeline.AddPass<RngExpander>();
    pipeline.AddPass<RngBitGeneratorExpander>(RandomAlgorithm::RNG_PHILOX);

    // Comparison total order expander
    pipeline.AddPass<ComparisonExpander>();

    // Remove zero-sized HLO from the input so that other passes don't have to
    // handle it.
    pipeline.AddPass<ZeroSizedHloElimination>();

    pipeline.AddPass<GpuScatterExpander>();
    // TODO(phawkins): replace QR decompositions with calls to cuSOLVER.
    pipeline.AddPass<QrExpander>();

    pipeline.AddPass<DynamicIndexSplitter>();

    // TODO(b/64094172): make Call work on GPU instead of inlining.
    pipeline.AddPass<CallInliner>();

    pipeline.AddPass<DotDecomposer>();

    pipeline.AddPass<Convolution4DExpander>();

    // Expand the sort op to support stable sorting if required.
    pipeline.AddPass<StableSortExpander>();
    // Convert BF16 operations to F32 operations so that the GPU backend can
    // support BF16 operations without directly implementing a BF16 lowering for
    // most ops.
    pipeline.AddPass<HloElementTypeConverter>(BF16, F32);

    // If cudnn batchnorms are enabled, rewrite batchnorm HLOs to cudnn calls
    // where possible.  Not every batchnorm op can be implemented as a call to
    // cudnn, so decompose any remaining batchnorm ops into a soup of HLOs.
    if (hlo_module->config().debug_options().xla_gpu_use_cudnn_batchnorm()) {
      // Since BatchNorm inference is essentially pointwise operations, it is
      // always advantageous to use kernel fusion rather than cudnn.
      pipeline.AddPass<BatchNormExpander>(
          /*rewrite_training_op=*/false,
          /*rewrite_inference_op=*/true,
          /*rewrite_grad_op=*/false);
      pipeline.AddPass<CudnnBatchNormRewriter>();
    }
    pipeline.AddPass<BatchNormExpander>(
        /*rewrite_training_op=*/true,
        /*rewrite_inference_op=*/true,
        /*rewrite_grad_op=*/true);

    pipeline.AddPass<LogisticExpander>(
        /*expansion_type=*/LogisticExpansionType::kExp);
    pipeline.AddPass<ConditionalCanonicalizer>();
    pipeline.AddPass<DynamicPadder>();

    {
      auto& pass =
          pipeline.AddPass<HloPassFix<HloPassPipeline>>("simplification");
      pass.AddInvariantCheckerDebug<HloVerifier>(
          /*layout_sensitive=*/false,
          /*allow_mixed_precision=*/false);

      // BatchNormExpander can create zero-sized ops, so zero-sized HLO
      // elimination has to come after that pass.
      pass.AddPass<ZeroSizedHloElimination>();

      pass.AddPass<GatherExpander>(GatherExpander::kEliminateSimpleGathers);
      pass.AddPass<ScatterExpander>(ScatterExpander::kEliminateSimpleScatters);

      AlgebraicSimplifierOptions options;
      // When transposes appear in a fusion node, we can easily adjust the
      // multi-dimensional index to create the one needed for the operand. This
      // is not as easy with bitcasts, because we don't have the information
      // readily available which dimensions are permuted. In addition to that,
      // if we have a transpose and a reshape next to each other, they will both
      // be replaced by a bitcast, and we replace bitcast(bitcast) with one
      // bitcast. This leads to having to linearize and then delinearize the
      // index.
      options.set_replace_transpose_with_bitcast(false);
      options.set_enable_conv_operand_swap(false);
      pass.AddPass<AlgebraicSimplifier>(options);
      // AlgebraicSimplifier may add contracting dimensions to a dot.
      pass.AddPass<DotDecomposer>();
      pass.AddPass<SortSimplifier>();
      pass.AddPass<TupleSimplifier>();
      pass.AddPass<WhileLoopConstantSinking>();
      pass.AddPass<WhileLoopSimplifier>();

      // TODO(b/134075051): Re-enable after b/134075051 is fixed.
      // pass.AddPass<SliceSinker>();

      pass.AddPass<HloDCE>();
      pass.AddPass<ReshapeMover>();
      pass.AddPass<HloConstantFolding>();
      pass.AddPass<ConditionalSimplifier>();
    }

    pipeline.AddPass<TransposeFolding>(
        [](const HloInstruction& dot,
           const TransposeFolding::OperandIndices& candidate_operands) {
          return IsMatrixMultiplication(dot)
                     ? candidate_operands
                     : TransposeFolding::OperandIndices{};
        });
    pipeline.AddPass<HloCSE>(/*is_layout_sensitive=*/false);
    pipeline.AddPass<HloDCE>();

    // Run WhileLoopTripCountAnnotator at the end of the simplification
    // pipeline, before layout assignment and fusion.  This pass does some
    // pattern-matching on while bodies/conditions, and this is where the HLO is
    // "nicest".
    //
    // It's important that we don't make semantic changes (e.g. unrolling) to
    // any `while` loops after this point, because otherwise the trip-count
    // annotations added by this pass may not be correct after the
    // modifications.
    pipeline.AddPass<WhileLoopTripCountAnnotator>();
    TF_RETURN_IF_ERROR(pipeline.Run(hlo_module).status());
  }

  // Run target-specific HLO optimization passes for convolution
  // canonicalization.
  TF_RETURN_IF_ERROR(OptimizeHloConvolutionCanonicalization(
      hlo_module, stream_exec, device_allocator));

  {
    // Run layout assignment in a separate pipeline from
    // "post-layout-assignment" because we want everything after layout
    // assignment to have a layout-sensitive invariant-checker, but
    // HloPassPipeline also runs its invariant checker before any passes are
    // run, meaning, the pipeline that contains layout assignment cannot contain
    // a layout-sensitive verifier!
    HloPassPipeline pipeline("layout assignment");
    // Layout assignment uses alias analysis, which requires the call graph to
    // be flattened.
    pipeline.AddPass<FlattenCallGraph>();
    pipeline.AddPass<GpuLayoutAssignment>(
        hlo_module->mutable_entry_computation_layout(),
        LayoutAssignment::InstructionCanChangeLayout, stream_exec);
    TF_RETURN_IF_ERROR(pipeline.Run(hlo_module).status());
  }

  // Run target-specific HLO optimization passes after layout assignment.
  TF_RETURN_IF_ERROR(OptimizeHloPostLayoutAssignment(hlo_module, stream_exec,
                                                     device_allocator));

  {
    HloPassFix<HloPassPipeline> fusion("fusion");
    // We try to split variadic ops with many parameters into several such ops
    // to avoid exceeding the parameter space.
    fusion.AddPass<VariadicOpSplitter>();
    /* TODO(b/117531509): Use LayoutAssignment::InstructionCanChangeLayout after
     * fixing the ticket. */
    fusion.AddInvariantCheckerDebug<HloVerifier>(
        /*layout_sensitive=*/true,
        /*allow_mixed_precision=*/false,
        LayoutAssignment::InstructionCanChangeLayout);
    fusion.AddPass<GpuInstructionFusion>(/*may_duplicate=*/false);
    fusion.AddPass<GpuInstructionFusion>(/*may_duplicate=*/true);
    fusion.AddPass<FusionMerger>();
    fusion.AddPass<GpuMultiOutputFusion>();
    fusion.AddPass<HloCSE>(/*is_layout_sensitive=*/true,
                           /*only_fusion_computations=*/true);
    fusion.AddPass<HloDCE>();
    TF_RETURN_IF_ERROR(fusion.Run(hlo_module).status());

    HloPassPipeline horizontal_fusion("horizontal_fusion");
    horizontal_fusion.AddPass<GpuHorizontalLoopFusion>();
    horizontal_fusion.AddPass<GpuHorizontalInputFusion>();
    horizontal_fusion.AddPass<HloCSE>(/*is_layout_sensitive=*/true,
                                      /*only_fusion_computations=*/true);
    horizontal_fusion.AddPass<HloDCE>();
    TF_RETURN_IF_ERROR(horizontal_fusion.Run(hlo_module).status());
  }

  {
    HloPassPipeline pipeline("all_reduce_combiner");
    pipeline.AddPass<AllReduceCombiner>(
        /*combine_threshold_in_bytes=*/30 * 1024 * 1024,
        /*combine_threshold_count=*/256);
    TF_RETURN_IF_ERROR(pipeline.Run(hlo_module).status());
  }
  {
    // Now we allow to replace any transposes outside of fusions with bitcasts.
    HloPassPipeline pipeline("final_algebraic_simplifier");
    AlgebraicSimplifierOptions options;
    options.set_is_layout_sensitive(true);
    options.set_enable_conv_operand_swap(false);
    pipeline.AddPass<AlgebraicSimplifier>(options);
    TF_RETURN_IF_ERROR(pipeline.Run(hlo_module).status());
  }
  return Status::OK();
}

// Modifies the given HLO module so that it will be accepted by IrEmitter.
// Unlike optimization passes, the passes are necessary for correctness.
Status GpuCompiler::PrepareHloModuleForIrEmitting(HloModule* hlo_module) {
  // In some cases, we have to place the result of an instruction in a temporary
  // buffer. For instance, the buffer that holds an external parameter is
  // assumed immutable at this point, and should not be reused for output
  // (b/27180329). Therefore, in that case, we set the output to be a copy of
  // the parameter.
  HloPassPipeline pipeline("GPU-ir-emit-prepare");
  /* TODO(b/117531509): Use LayoutAssignment::InstructionCanChangeLayout after
   * fixing the ticket. */
  pipeline.AddInvariantCheckerDebug<HloVerifier>(
      /*layout_sensitive=*/true,
      /*allow_mixed_precision=*/false,
      LayoutAssignment::InstructionCanChangeLayout);

  // Copy insertion should be performed immediately before IR emission to avoid
  // inserting unnecessary copies (later pass adds an instruction which
  // materializes the value) or missing a necessary copy (later pass removes an
  // instruction which materializes a value). DCE must be run immediately before
  // (and sometime after) copy insertion, to avoid dead code from interfering
  // with the rewrites.
  pipeline.AddPass<HloDCE>();
  if (hlo_module->config().alias_passthrough_params()) {
    pipeline.AddPass<AliasPassthroughParams>();
  }
  pipeline.AddPass<GpuCopyInsertion>(GetCanShareBuffer());
  pipeline.AddPass<GpuSanitizeConstantNames>();
  return pipeline.Run(hlo_module).status();
}

// TODO(cheshire): Duplication with gpu_conv_algorithm picker, figure out a
// right way to share this.
static bool RequireDeterminism() {
  static bool require_determinism = [] {
    bool deterministic_ops = false;
    TF_CHECK_OK(tensorflow::ReadBoolFromEnvVar("TF_DETERMINISTIC_OPS",
                                               /*default_val=*/false,
                                               &deterministic_ops));
    return deterministic_ops;
  }();
  return require_determinism;
}

Status GpuCompiler::OptimizeHloPostLayoutAssignment(
    HloModule* hlo_module, se::StreamExecutor* stream_exec,
    se::DeviceMemoryAllocator* device_allocator) {
  HloPassPipeline pipeline("post-layout_assignment");
  /* TODO(b/117531509): Use LayoutAssignment::InstructionCanChangeLayout after
   * fixing the ticket. */
  pipeline.AddInvariantCheckerDebug<HloVerifier>(
      /*layout_sensitive=*/true,
      /*allow_mixed_precision=*/false,
      LayoutAssignment::InstructionCanChangeLayout);

  pipeline.AddPass<ReductionDegenerateDimRemover>();
  pipeline.AddPass<ReductionLayoutNormalizer>();
  pipeline.AddPass<ReductionDimensionGrouper>();
  pipeline.AddPass<HloPassFix<ReductionSplitter>>();

  // The LayoutAssignment pass may leave behind kCopy instructions which are
  // duplicate or NOPs, so remove them with algebraic simplification and CSE.
  AlgebraicSimplifierOptions options;
  options.set_is_layout_sensitive(true);
  // When transposes appear in a fusion node, we can easily adjust the
  // multi-dimensional index to create the one needed for the operand. This
  // is not as easy with bitcasts, because we don't have the information
  // readily available which dimensions are permuted. In addition to that,
  // if we have a transpose and a reshape next to each other, they will both
  // be replaced by a bitcast, and we replace bitcast(bitcast) with one
  // bitcast. This leads to having to linearize and then delinearize the
  // index.
  options.set_replace_transpose_with_bitcast(false);
  options.set_enable_conv_operand_swap(false);
  pipeline.AddPass<HloPassFix<AlgebraicSimplifier>>(options);

  if (RequireDeterminism() ||
      hlo_module->config().debug_options().xla_gpu_deterministic_reductions()) {
    pipeline.AddPass<HloPassFix<GpuTreeReductionRewriter>>();
  }

  // GemmRewriter assumes that all transposes are folded into gemms, but,
  // since commit 7d529df, this is not always true at this point.
  // Therefore, rerun transpose folding.
  pipeline.AddPass<TransposeFolding>(
      [](const HloInstruction& dot,
         const TransposeFolding::OperandIndices& candidate_operands) {
        return IsMatrixMultiplication(dot) ? candidate_operands
                                           : TransposeFolding::OperandIndices{};
      },
      TransposeFolding::NeverFoldTranspose);
  // Rewrite GEMMs into custom calls.
  pipeline.AddPass<GemmRewriter>();

  // Choose the fastest algorithm for each conv.
  //
  // We pick the algorithm before fusion so we can generate better HLO. After
  // GpuConvRewriter, our convolutions are CustomCalls which return a
  // tuple (conv_result, scratch_memory), and the each conv uses 0 bytes of
  // scratch:
  //
  //   customcall = (f32[...], f32[0])
  //   return gte(customcall, 0)
  //
  // The algorithm picker then chooses the best algorithm, and potentially
  // increases the scratch space.  It replaces customcall with new_tuple,
  // giving us the following:
  //
  //   new_customcall = (f32[...], f32[N])
  //   new_tuple = tuple(gte(new_customcall, 0), constant f32[0])
  //   return gte(new_tuple, 0)
  //
  // The new tuple and gte instructions then be simplified away, because
  // nobody is expected to use the scratch value.
  //
  // However, if we were to run GpuConvAlgorithmPicker after fusion
  // the gte(customcall, 0) would probably already be into a fusion node.  We
  // can't simplify across HloComputation boundaries, so in this case we
  // wouldn't be able to simplify away the new_tuple bits.
  pipeline.AddPass<GpuConvAlgorithmPicker>(stream_exec, device_allocator);

  // Clean up new_tuple described above.
  pipeline.AddPass<TupleSimplifier>();

  pipeline.AddPass<HloCSE>(/*is_layout_sensitive=*/true);
  TF_RETURN_IF_ERROR(pipeline.Run(hlo_module).status());

  return Status::OK();
}

StatusOr<std::unique_ptr<HloModule>> GpuCompiler::RunHloPasses(
    std::unique_ptr<HloModule> module, se::StreamExecutor* stream_exec,
    se::DeviceMemoryAllocator* device_allocator) {
  // We dump the post-optimization HLO in RunBackend so no need to dump it here.
  XLA_SCOPED_LOGGING_TIMER("GpuCompiler::RunHloPasses");
  tensorflow::profiler::TraceMe activity(
      [&] { return absl::StrCat("HLO Transforms:", module->name()); },
      tensorflow::profiler::TraceMeLevel::kInfo);
  TF_RETURN_IF_ERROR(
      OptimizeHloModule(module.get(), stream_exec, device_allocator));

  TF_RETURN_IF_ERROR(PrepareHloModuleForIrEmitting(module.get()));

  return std::move(module);
}

static absl::optional<bool> DummyCanShareBufferFunction(const HloInstruction*,
                                                        const HloInstruction*,
                                                        const ShapeIndex&) {
  return absl::nullopt;
}

StatusOr<
    std::tuple<std::unique_ptr<HloModule>, std::unique_ptr<BufferAssignment>>>
GpuCompiler::RunHloPassesAndBufferAssignement(
    std::unique_ptr<HloModule> hlo_module, se::StreamExecutor* executor,
    se::DeviceMemoryAllocator* device_allocator, bool optimize) {
  if (optimize) {
    TF_ASSIGN_OR_RETURN(hlo_module, RunHloPasses(std::move(hlo_module),
                                                 executor, device_allocator));
  }

  std::unique_ptr<StreamAssignment> stream_assignment =
      AssignStreams(*hlo_module);
  TF_ASSIGN_OR_RETURN(
      std::unique_ptr<GpuHloSchedule> hlo_schedule,
      GpuHloSchedule::Build(*hlo_module, *stream_assignment, pointer_size_));

  auto buffer_size_bytes_function =
      [this](const BufferValue& buffer_value) -> int64 {
    return GpuCompiler::GetSizeOfShape(buffer_value.shape(), pointer_size_);
  };

  TF_ASSIGN_OR_RETURN(
      std::unique_ptr<BufferAssignment> assignment,
      BufferAssigner::Run(
          hlo_module.get(), hlo_schedule->ConsumeHloOrdering(),
          buffer_size_bytes_function,
          /*color_alignment=*/
          [](LogicalBuffer::Color) { return kXlaAllocatedBufferAlignBytes; },
          /*allocate_buffers_for_constants=*/true,
          /*colorer=*/BufferAssigner::DefaultColorer(),
          /*must_not_live_out=*/{}, DummyCanShareBufferFunction));

  return std::make_tuple(std::move(hlo_module), std::move(assignment));
}

// The order of `thunk_sequence` corresponds to
// `hlo_schedule->ThunkLaunchOrder()`.
static Status CompileModuleToLlvmIrImpl(
    HloModule* hlo_module, llvm::LLVMContext* llvm_context,
    const std::string& target_triple, const std::string& data_layout,
    const std::string& platform_name, GpuDeviceInfo gpu_device_info,
    absl::optional<CudaComputeCapability> cuda_compute_capability,
    const HloDataflowAnalysis::CanShareBuffer& can_share_buffer_function,
    int pointer_size, const HloProfileIndexMap* profile_index_map,
    std::unique_ptr<llvm::Module>* llvm_module,
    std::unique_ptr<BufferAssignment>* buffer_assignment,
    std::unique_ptr<ThunkSchedule>* thunk_schedule,
    std::vector<GpuExecutable::ConstantInfo>* constants) {
  *llvm_module = absl::make_unique<llvm::Module>("", *llvm_context);

  (*llvm_module)->setTargetTriple(target_triple);
  (*llvm_module)->setDataLayout(data_layout);

  std::unique_ptr<StreamAssignment> stream_assignment =
      AssignStreams(*hlo_module);
  TF_ASSIGN_OR_RETURN(
      std::unique_ptr<GpuHloSchedule> hlo_schedule,
      GpuHloSchedule::Build(*hlo_module, *stream_assignment, pointer_size));

  auto buffer_size_bytes_function =
      [pointer_size](const BufferValue& buffer_value) -> int64 {
    return GpuCompiler::GetSizeOfShape(buffer_value.shape(), pointer_size);
  };

  TF_ASSIGN_OR_RETURN(
      *buffer_assignment,
      BufferAssigner::Run(
          hlo_module, hlo_schedule->ConsumeHloOrdering(),
          buffer_size_bytes_function,
          /*color_alignment=*/
          [](LogicalBuffer::Color) { return kXlaAllocatedBufferAlignBytes; },
          /*allocate_buffers_for_constants=*/true,
          /*colorer=*/BufferAssigner::DefaultColorer(),
          /*must_not_live_out=*/{}, can_share_buffer_function));

  VLOG(1) << "Buffer Assignment Stats "
          << (*buffer_assignment)->GetStats().ToString();
  DumpHloModuleIfEnabled(*hlo_module, **buffer_assignment,
                         "after_optimizations");

  mlir::MLIRContext mlir_context;

  IrEmitterContext ir_emitter_context(
      hlo_module, buffer_assignment->get(), platform_name, gpu_device_info,
      cuda_compute_capability, profile_index_map, &mlir_context,
      llvm_module->get());

  HloComputation* entry_computation = hlo_module->entry_computation();

  TF_ASSIGN_OR_RETURN(
      auto ir_emitter,
      IrEmitterUnnested::Create(hlo_module->config(), entry_computation,
                                &ir_emitter_context));

  {
    XLA_SCOPED_LOGGING_TIMER("GpuCompiler::RunBackend - IR emission");

    absl::flat_hash_map<const Thunk*, const HloInstruction*> thunk_to_hlo;
    ThunkSequence thunk_sequence;
    absl::Span<HloInstruction* const> order = hlo_schedule->ThunkLaunchOrder();
    for (HloInstruction* instruction : order) {
      TF_RETURN_IF_ERROR(instruction->Visit(ir_emitter.get()));
      TF_RETURN_IF_ERROR(ir_emitter->Postprocess(instruction));
      std::unique_ptr<ThunkSequence> thunks =
          ir_emitter->ConsumeThunkSequence();

      // The invariants between each input HloInstruction* and output Thunk* are
      // not all explicitly checked, but at least we can document them here:
      // * The entry HloComputation shall not have dead code (all reachable from
      // ROOT).
      // * The visited instructions are all instructions in the entry
      // computation.
      // * For each visit of these HloInstructions, either none or one Thunk
      // will be returned.
      // * If there is a thunk returned, thunk->hlo_instruction_ equals the
      // input HloInstruction*.
      // * A returned thunk may contain other sub-thunks. A sub-thunk may or may
      // not have an associated hlo_instruction_.
      TF_RET_CHECK(thunks->size() <= 1) << instruction->ToString();
      if (!thunks->empty()) {
        auto thunk = std::move(thunks->front());
        InsertOrDie(&thunk_to_hlo, thunk.get(), instruction);
        thunk_sequence.push_back(std::move(thunk));
      }
    }
    // TODO(timshen): ThunkSchedule taking thunk_to_hlo is a bit awkward. To fix
    // that, we can turn it into a proper pass, from:
    //   map<Thunk, HloInstruction> -> (ThunkSchedule, [Thunk...])
    // to:
    //   map<Thunk, HloInstruction> -> GenerateMultiStreamDepInfo() -> [(Thunk,
    //   DepInfo)...]
    //
    //   where "DepInfo" is
    //   struct {
    //     int stream_number;
    //     std::vector<Thunk*> dependencies;
    //     std::vector<Thunk*> users;
    //   };
    // We might want to do this after MLIR migration.
    *thunk_schedule = absl::make_unique<ThunkSchedule>(
        std::make_unique<ThunkSequence>(std::move(thunk_sequence)),
        std::move(stream_assignment), std::move(thunk_to_hlo));

    if (constants) {
      *constants = std::move(ir_emitter_context.constants());
    }
  }

  return Status::OK();
}

StatusOr<std::unique_ptr<Executable>> GpuCompiler::RunBackend(
    std::unique_ptr<HloModule> module, se::StreamExecutor* stream_exec,
    se::DeviceMemoryAllocator* device_allocator) {
  XLA_SCOPED_LOGGING_TIMER("GpuCompiler::RunBackend");
  auto slow_compile_alarm = SlowCompilationAlarm();

  TF_RET_CHECK(stream_exec != nullptr);

  llvm::LLVMContext llvm_context;
  std::string buffer;
  llvm::raw_string_ostream error(buffer);
  llvm::DiagnosticPrinterRawOStream printer(error);
  auto DiagnosticHandler = [](const llvm::DiagnosticInfo& diag_info,
                              void* Context) {
    auto printer = static_cast<llvm::DiagnosticPrinterRawOStream*>(Context);
    diag_info.print(*printer);
  };
  llvm_context.setDiagnosticHandlerCallBack(DiagnosticHandler, &printer);

  GpuDeviceInfo gpu_device_info;
  gpu_device_info.threads_per_block_limit =
      stream_exec->GetDeviceDescription().threads_per_block_limit();
  gpu_device_info.threads_per_warp =
      stream_exec->GetDeviceDescription().threads_per_warp();
  gpu_device_info.shared_memory_per_block =
      stream_exec->GetDeviceDescription().shared_memory_per_block();
  gpu_device_info.threads_per_core_limit =
      stream_exec->GetDeviceDescription().threads_per_core_limit();
  gpu_device_info.core_count = stream_exec->GetDeviceDescription().core_count();

  absl::optional<CudaComputeCapability> cuda_compute_capability =
      [&]() -> absl::optional<CudaComputeCapability> {
    CudaComputeCapability cuda_compute_capability;
    stream_exec->GetDeviceDescription().cuda_compute_capability(
        &cuda_compute_capability.cc_major, &cuda_compute_capability.cc_minor);
    if (cuda_compute_capability.cc_major == -1) {
      return absl::nullopt;
    }
    return cuda_compute_capability;
  }();

  std::unique_ptr<HloProfileIndexMap> profile_index_map;
  std::unique_ptr<HloProfilePrinterData> profile_printer;

  if (module->config().hlo_profiling_enabled() || VLOG_IS_ON(1)) {
    HloCostAnalysis cost_analysis(ShapeSizeBytesFunction());
    cost_analysis.set_bytes_per_second(
        stream_exec->GetDeviceDescription().memory_bandwidth());
    TF_RETURN_IF_ERROR(module->entry_computation()->Accept(&cost_analysis));
    VLOG(1) << "HLO memory read+written: "
            << tensorflow::strings::HumanReadableNumBytes(
                   cost_analysis.bytes_accessed());
    if (module->config().hlo_profiling_enabled()) {
      profile_index_map = absl::make_unique<HloProfileIndexMap>(*module);
      profile_printer =
          CreateHloProfilePrinterData(*profile_index_map, cost_analysis,
                                      module->entry_computation()->name());
    }
  }

  std::unique_ptr<llvm::Module> llvm_module;
  std::unique_ptr<BufferAssignment> buffer_assignment;
  std::unique_ptr<ThunkSchedule> thunk_schedule;
  std::vector<GpuExecutable::ConstantInfo> constants;

  TF_RETURN_IF_ERROR(CompileModuleToLlvmIrImpl(
      module.get(), &llvm_context, target_triple_, data_layout_,
      stream_exec->platform()->Name(), gpu_device_info, cuda_compute_capability,
      GetCanShareBuffer(), pointer_size_, profile_index_map.get(), &llvm_module,
      &buffer_assignment, &thunk_schedule, &constants));

  if (user_pre_optimization_hook_) {
    user_pre_optimization_hook_(*llvm_module);
  }
  string ir_module_string_before_opt;
  const bool embed_ir_in_executable =
      module->config().debug_options().xla_embed_ir_in_executable();
  if (embed_ir_in_executable) {
    ir_module_string_before_opt = llvm_ir::DumpModuleToString(*llvm_module);
  }

  llvm_ir::DumpIrIfEnabled(*module, *llvm_module, /*optimized=*/false);

  {
    XLA_SCOPED_LOGGING_TIMER("GpuCompiler::RunBackend - Running LLVM verifier");

    std::string err;
    llvm::raw_string_ostream err_stream(err);

    // verifyModule() returns true if the module is broken.
    TF_RET_CHECK(!llvm::verifyModule(*llvm_module, &err_stream))
        << "Invalid LLVM IR before optimizations:\n"
        << err_stream.str()
        << "\nThis probably indicates a bug in the HLO -> LLVM IR lowering. "
           "Rerun with --xla_dump_to to get the IR and looks for files with "
           "name containing: *"
        << FilenameFor(*module, "", "") << "*";
  }

  GpuVersion gpu_version = GetGpuVersion(stream_exec);

  using BackendCompileResult = std::pair<std::string, std::vector<uint8>>;
  TF_ASSIGN_OR_RETURN(BackendCompileResult backend_result,
                      CompileTargetBinary(module.get(), llvm_module.get(),
                                          gpu_version, stream_exec));

  if (DumpingEnabledForHloModule(*module)) {
    DumpToFileInDirOrStdout(*module, "", "thunk_schedule",
                            thunk_schedule->ToString());
  }

  auto* gpu_executable = new GpuExecutable(
      backend_result.first, backend_result.second, gpu_version,
      std::move(thunk_schedule), std::move(module),
      std::move(buffer_assignment), std::move(profile_printer),
      std::move(profile_index_map), std::move(constants));
  if (embed_ir_in_executable) {
    DCHECK_NE("", ir_module_string_before_opt);
    gpu_executable->set_ir_module_string(ir_module_string_before_opt);
  }
  return std::unique_ptr<Executable>(gpu_executable);
}

StatusOr<std::vector<std::unique_ptr<AotCompilationResult>>>
GpuCompiler::CompileAheadOfTime(std::unique_ptr<HloModuleGroup> module_group,
                                const AotCompilationOptions& options) {
  return Unimplemented("not yet implemented: GpuCompiler::CompileAheadOfTime");
}

StatusOr<std::unique_ptr<llvm::Module>> CompileModuleToLlvmIr(
    HloModule* hlo_module, llvm::LLVMContext* llvm_context,
    const std::string& target_triple, const std::string& data_layout,
    const std::string& platform_name, GpuDeviceInfo gpu_device_info,
    absl::optional<CudaComputeCapability> cuda_compute_capability,
    int pointer_size) {
  std::unique_ptr<llvm::Module> llvm_module;
  std::unique_ptr<BufferAssignment> buffer_assignment;
  std::unique_ptr<ThunkSchedule> thunk_schedule;

  TF_RETURN_IF_ERROR(CompileModuleToLlvmIrImpl(
      hlo_module, llvm_context, target_triple, data_layout, platform_name,
      gpu_device_info, cuda_compute_capability, DummyCanShareBufferFunction,
      pointer_size, /*profile_index_map=*/nullptr, &llvm_module,
      &buffer_assignment, &thunk_schedule, nullptr));
  return llvm_module;
}
}  // namespace gpu
}  // namespace xla
