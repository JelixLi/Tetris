/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

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

// This file implements logic for lowering HLO/LHLO dialect to Linalg dialect.

#include <numeric>

#include "llvm/ADT/STLExtras.h"
#include "mlir-hlo/Dialect/mhlo/IR/hlo_ops.h"
#include "mlir-hlo/Dialect/mhlo/IR/lhlo_ops.h"
#include "mlir-hlo/Dialect/mhlo/transforms/map_lmhlo_to_scalar_op.h"
#include "mlir-hlo/Dialect/mhlo/transforms/rewriters.h"
#include "mlir/Dialect/Affine/IR/AffineOps.h"
#include "mlir/Dialect/Linalg/IR/LinalgOps.h"
#include "mlir/Dialect/Linalg/IR/LinalgTypes.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/IR/AffineExpr.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Function.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/OperationSupport.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/StandardTypes.h"
#include "mlir/IR/TypeUtilities.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

namespace mlir {
namespace {

SmallVector<StringRef, 3> GetNParallelLoopsAttrs(unsigned nParallelLoops) {
  static constexpr StringRef kParallelIterType = "parallel";
  return SmallVector<StringRef, 3>(nParallelLoops, kParallelIterType);
}

template <bool isLHLO = true>
Value getResultValue(Operation* op) {
  return isLHLO ? op->getOperand(op->getNumOperands() - 1) : op->getResult(0);
}

template <bool isLHLO = true>
ShapedType getHloOpResultType(Operation* op) {
  return getResultValue<isLHLO>(op).getType().template cast<ShapedType>();
}

template <bool isLHLO = true>
bool verifyHloOpBufferOrTensorSemantics(Operation* op) {
  auto verifyType = [&](Value val) -> bool {
    return (isLHLO && val.getType().isa<MemRefType>()) ||
           (!isLHLO && val.getType().isa<RankedTensorType>());
  };
  if (!llvm::all_of(op->getOperands(), verifyType)) return false;
  return isLHLO ? op->getResults().empty()
                : llvm::all_of(op->getResults(), verifyType);
}

template <typename OpTy, bool isLHLO = true>
class PointwiseToLinalgConverter : public OpConversionPattern<OpTy> {
 public:
  using OpConversionPattern<OpTy>::OpConversionPattern;

  LogicalResult matchAndRewrite(
      OpTy op, ArrayRef<Value> args,
      ConversionPatternRewriter& rewriter) const final {
    auto loc = op.getLoc();
    ShapedType t0 = args[0].getType().template dyn_cast<ShapedType>();
    if (!t0) return failure();

    unsigned nloops = t0.getRank();
    auto fail = [&](ShapedType t) {
      return !t || !t.hasRank() || t.getRank() != nloops ||
             !(t.getElementType().isSignlessIntOrFloat() ||
               t.getElementType().isa<ComplexType>());
    };
    if (llvm::any_of(args,
                     [&](Value v) {
                       return fail(v.getType().dyn_cast<ShapedType>());
                     }) ||
        llvm::any_of(op.getOperation()->getResultTypes(),
                     [&](Type t) { return fail(t.dyn_cast<ShapedType>()); }))
      return emitError(loc,
                       "lhlo to linalg conversion expects ranked args of "
                       "signless int, float or complex element type with ")
             << nloops << " parallel iterators: " << *(op.getOperation());

    // Construct the indexing maps needed for linalg.generic ops.
    SmallVector<Type, 4> bodyArgTypes, bodyResultTypes, opResultTypes;

    // This doesnt account for implicit broadcast, but the working assumption
    // in HLO/LHLO is that are broadcasts are made explicit.

    if (isLHLO && !nloops) return failure();

    int numInputs = (isLHLO ? args.size() - 1 : args.size());

    ValueRange inputs(args.take_front(numInputs));
    for (Value in : inputs)
      bodyArgTypes.emplace_back(getElementTypeOrSelf(in.getType()));

    ValueRange outputBuffers(args.take_back(args.size() - numInputs));
    for (Value out : outputBuffers)
      bodyResultTypes.emplace_back(getElementTypeOrSelf(out.getType()));

    if (!isLHLO) {
      // HLO operations have return as tensor types.
      assert(bodyResultTypes.empty() &&
             "When lowering HLO ops result can't be part of arguments");
      Value result = op.getOperation()->getResult(0);
      bodyResultTypes.push_back(getElementTypeOrSelf(result));
      opResultTypes.push_back(result.getType());
    }

    AffineMap commonIndexingMap =
        nloops ? rewriter.getMultiDimIdentityMap(nloops)
               : AffineMap::get(nloops, 0, rewriter.getContext());
    SmallVector<AffineMap, 2> indexing_maps(args.size() + (isLHLO ? 0 : 1),
                                            commonIndexingMap);

    auto linalgOp = rewriter.create<linalg::GenericOp>(
        loc, opResultTypes, inputs, outputBuffers,
        /*initTensors=*/ValueRange{}, indexing_maps,
        GetNParallelLoopsAttrs(nloops),
        [&](OpBuilder& nestedBuilder, Location nestedLoc, ValueRange args) {
          // TODO(ravishankarm) : For now use the method in lmhlo namespace.
          // That method needs to be moved out of there.
          Value opResult = lmhlo::HloOpToStdScalarOp::map<OpTy>(
              op, bodyResultTypes,
              llvm::to_vector<2>(args.take_front(inputs.size())), &rewriter);
          nestedBuilder.create<linalg::YieldOp>(loc, opResult);
        });
    rewriter.replaceOp(op, linalgOp.getOperation()->getResults());
    return success();
  }
};

template <typename LhloOp>
class ScalarPointwiseToStandardConverter : public OpConversionPattern<LhloOp> {
 public:
  using OpConversionPattern<LhloOp>::OpConversionPattern;

  LogicalResult matchAndRewrite(
      LhloOp lhlo_op, ArrayRef<Value> args,
      ConversionPatternRewriter& rewriter) const final {
    auto loc = lhlo_op.getLoc();
    auto argType =
        lhlo_op.getOperand(0).getType().template dyn_cast<ShapedType>();
    if (!argType || !argType.getElementType().isSignlessIntOrFloat() ||
        (argType.getRank() != 0)) {
      return failure();
    }

    // Create two loads from the input.
    auto lhs = rewriter.create<LoadOp>(loc, lhlo_op.lhs());
    auto rhs = rewriter.create<LoadOp>(loc, lhlo_op.rhs());
    // TODO(ravishankarm) : Move this method out of lmhlo namespace.
    Value opResult = lmhlo::HloOpToStdScalarOp::map<LhloOp>(
        lhlo_op, argType.getElementType(), llvm::ArrayRef<Value>{lhs, rhs},
        &rewriter);
    rewriter.create<StoreOp>(loc, opResult, lhlo_op.out());
    rewriter.eraseOp(lhlo_op);
    return success();
  }
};

//===----------------------------------------------------------------------===//
// lmhlo.convolution conversion pattern.
//===----------------------------------------------------------------------===//

/// Converts lmhlo.convolution operation to a linalg.conv op.
struct ConvToLinalgConverter : public OpConversionPattern<lmhlo::ConvOp> {
 public:
  using OpConversionPattern<lmhlo::ConvOp>::OpConversionPattern;

  //  This code has been adapted from IREE's
  //  (https://github.com/google/iree/) mhlo -> linalg conversion.
  LogicalResult matchAndRewrite(
      lmhlo::ConvOp op, ArrayRef<Value> args,
      ConversionPatternRewriter& rewriter) const final {
    // Check validity of dimension information.
    if (const mhlo::ConvDimensionNumbers& dimensionNumbers =
            op.dimension_numbers()) {
      const int inputSpatialRank =
          llvm::size(dimensionNumbers.input_spatial_dimensions());
      // The dimensions for input should follow the order of
      // batch_count, spatial_dims..., input_feature_count.
      if (dimensionNumbers.input_batch_dimension().getInt() != 0 ||
          dimensionNumbers.input_feature_dimension().getInt() !=
              (inputSpatialRank + 1))
        return failure();

      const int kernelSpatialRank =
          llvm::size(dimensionNumbers.kernel_spatial_dimensions());
      // The dimensions for filter should follow the order of
      // spatial_dims..., input_feature_count, num_output_feature_count.
      if (dimensionNumbers.kernel_input_feature_dimension().getInt() !=
              kernelSpatialRank ||
          dimensionNumbers.kernel_output_feature_dimension().getInt() !=
              (kernelSpatialRank + 1))
        return failure();

      const int outputSpatialRank =
          llvm::size(dimensionNumbers.output_spatial_dimensions());
      // The dimensions for output should follow the order of
      // batch_count, spatial_dims.., output_feature_count.
      if (dimensionNumbers.output_batch_dimension().getInt() != 0 ||
          dimensionNumbers.output_feature_dimension().getInt() !=
              (outputSpatialRank + 1))
        return failure();

      if (inputSpatialRank != outputSpatialRank ||
          inputSpatialRank != kernelSpatialRank)
        return failure();

      auto inputSpatialDim =
          dimensionNumbers.input_spatial_dimensions().begin();
      auto kernelSpatialDim =
          dimensionNumbers.kernel_spatial_dimensions().begin();
      auto outputSpatialDim =
          dimensionNumbers.output_spatial_dimensions().begin();
      // Check if spatial dims are ordered correctly.
      for (int i = 0; i < inputSpatialRank; ++i) {
        const int dim = i + 1;
        if ((*inputSpatialDim++).getZExtValue() != dim ||
            (*outputSpatialDim++).getZExtValue() != dim ||
            (*kernelSpatialDim++).getZExtValue() != i)
          return failure();
      }
    }

    // TODO: LHS dilation for deconvolution not supported yet.
    if (op.lhs_dilation()) {
      return failure();
    }

    llvm::SmallVector<Attribute, 4> strides;
    if (auto windowStrides = op.window_strides()) {
      auto range = windowStrides->getAttributeValues();
      strides.assign(range.begin(), range.end());
    }
    auto stridesArg = ArrayAttr::get(strides, op.getContext());

    llvm::SmallVector<Attribute, 2> dilation;
    if (auto rhsDilation = op.rhs_dilation()) {
      auto range = rhsDilation->getAttributeValues();
      dilation.assign(range.begin(), range.end());
    } else {
      // Default dilation of 1.
      dilation.resize(2, IntegerAttr::get(rewriter.getIntegerType(64), 1));
    }
    auto dilationArg = ArrayAttr::get(dilation, op.getContext());

    // Set padding only if it is non-zero.
    DenseIntElementsAttr padding = op.paddingAttr();
    if (!padding || !llvm::any_of(padding.getValues<APInt>(), [](APInt intVal) {
          return !intVal.isNullValue();
        })) {
      padding = nullptr;
    }

    // The order of input and filter are switched with linalg.conv.
    rewriter.replaceOpWithNewOp<linalg::ConvOp>(
        op, args[1], args[0], args[2], stridesArg, dilationArg, padding);
    return success();
  }
};

/// Base class for lowering HLO operations that have one operand and one result,
/// and are semantically equivalent to a copy of the input to the output (like
/// transpose, some reshape, etc.). The derived classes need to provide a method
/// `getIndexingMaps` that returns AffineMaps for the index maps of the input
/// and the output.
template <typename Derived, typename OpTy, bool isLHLO = true>
class DataMovementOpConverter : public OpConversionPattern<OpTy> {
 public:
  using OpConversionPattern<OpTy>::OpConversionPattern;

  LogicalResult matchAndRewrite(
      OpTy op, ArrayRef<Value> args,
      ConversionPatternRewriter& rewriter) const final {
    if (!verifyHloOpBufferOrTensorSemantics<isLHLO>(op)) return failure();
    auto resultType = getHloOpResultType<isLHLO>(op);

    SmallVector<AffineMap, 2> indexing_maps =
        Derived::getIndexingMaps(op, &rewriter);
    if (indexing_maps.empty()) return failure();

    auto nloops = resultType.getRank();
    auto loc = op.getLoc();
    auto linalgOp = rewriter.create<linalg::GenericOp>(
        loc,
        /*resultTensorTypes=*/isLHLO ? ArrayRef<Type>{} : resultType,
        /*inputs=*/args.front(),
        /*outputBuffers=*/isLHLO ? ValueRange{args.back()} : ValueRange{},
        /*initTensor=*/ValueRange{}, indexing_maps,
        GetNParallelLoopsAttrs(nloops),
        [&](OpBuilder& nestedBuilder, Location nestedLoc, ValueRange args) {
          nestedBuilder.create<linalg::YieldOp>(loc, *args.begin());
        });
    rewriter.replaceOp(op, linalgOp.getOperation()->getResults());
    return success();
  }
};

/// Pattern to convert BroadcastOp to Linalg ops.
template <typename OpTy, bool isLHLO = true>
class BroadcastConverter
    : public DataMovementOpConverter<BroadcastConverter<OpTy, isLHLO>, OpTy,
                                     isLHLO> {
 public:
  using DataMovementOpConverter<BroadcastConverter, OpTy,
                                isLHLO>::DataMovementOpConverter;

  static SmallVector<AffineMap, 2> getIndexingMaps(OpTy broadcastOp,
                                                   Builder* b) {
    ShapedType inputType =
        broadcastOp.operand().getType().template cast<ShapedType>();
    unsigned inputRank = inputType.getRank();
    unsigned nloops = getHloOpResultType<isLHLO>(broadcastOp).getRank();

    // BroadcastOp prepends the dimensions in the `broadcast_sizes` attribute to
    // the input's dimensions.
    unsigned numPrependedDims = llvm::size(broadcastOp.broadcast_sizes());
    SmallVector<AffineExpr, 4> inputDimExprs;
    inputDimExprs.reserve(inputRank);
    for (int i = 0; i < inputRank; ++i) {
      inputDimExprs.push_back(b->getAffineDimExpr(numPrependedDims + i));
    }

    AffineMap inputMap;
    MLIRContext* context = b->getContext();
    if (inputDimExprs.empty()) {
      // The input is a scalar, i.e. this is a scalar broadcast op.
      inputMap = AffineMap::get(nloops, /*symbolCount=*/0, context);
    } else {
      inputMap =
          AffineMap::get(nloops, /*symbolCount=*/0, inputDimExprs, context);
    }
    return {inputMap, b->getMultiDimIdentityMap(nloops)};
  }
};

class HloBroadcastInDimConverter
    : public DataMovementOpConverter<HloBroadcastInDimConverter,
                                     mhlo::BroadcastInDimOp, false> {
 public:
  using DataMovementOpConverter<HloBroadcastInDimConverter,
                                mhlo::BroadcastInDimOp,
                                false>::DataMovementOpConverter;

  static SmallVector<AffineMap, 2> getIndexingMaps(
      mhlo::BroadcastInDimOp broadcastOp, Builder* b) {
    auto resultType = getHloOpResultType<false>(broadcastOp);
    auto operandType =
        broadcastOp.operand().getType().template cast<ShapedType>();
    unsigned nloops = resultType.getRank();

    // The input is a scalar, i.e. this is a scalar broadcast op.
    if (operandType.getRank() == 0) {
      return {AffineMap::get(nloops, /*symbolCount=*/0, b->getContext()),
              b->getMultiDimIdentityMap(nloops)};
    }

    auto operandShape = operandType.getShape();
    SmallVector<AffineExpr, 4> dimExprs;
    dimExprs.reserve(nloops);

    if (broadcastOp.broadcast_dimensions()) {
      for (const auto& broadcastDim :
           enumerate(broadcastOp.broadcast_dimensions().getIntValues())) {
        int size = broadcastDim.value().getSExtValue();
        bool expansion_needed = operandShape[broadcastDim.index()] == 1 &&
                                resultType.getShape()[size] != 1;
        dimExprs.push_back(expansion_needed ? b->getAffineConstantExpr(0)
                                            : b->getAffineDimExpr(size));
      }
    }
    return {
        AffineMap::get(nloops, /*symbolCount=*/0, dimExprs, b->getContext()),
        b->getMultiDimIdentityMap(nloops)};
  }
};

class LhloBroadcastInDimConverter
    : public OpConversionPattern<lmhlo::BroadcastInDimOp> {
 public:
  using OpConversionPattern<lmhlo::BroadcastInDimOp>::OpConversionPattern;

  LogicalResult matchAndRewrite(
      lmhlo::BroadcastInDimOp op, ArrayRef<Value> args,
      ConversionPatternRewriter& rewriter) const final {
    lmhlo::BroadcastInDimOp::Adaptor operand_adaptor(args);
    auto result_type = operand_adaptor.output().getType().cast<MemRefType>();
    auto result_shape = result_type.getShape();

    auto operand_and_dims = InsertReshapeIfNecessary(op, args, rewriter);

    Value operand = std::get<0>(operand_and_dims);
    auto broadcast_dims = std::get<1>(operand_and_dims);

    auto loc = op.getLoc();
    auto nloops = result_type.getRank();
    auto operand_type = operand.getType().cast<MemRefType>();

    // For a degenerate case, i.e. broadcasting with expansion of
    // memref<1xELEMENT_TYPE>, the operand is not passed to `linalg.generic`.
    // Instead the value is loaded and used directly in `linalg.yield`.
    if (operand_type.getRank() == 1 &&
        operand_type.getDimSize(0) <
            result_type.getDimSize(broadcast_dims.front())) {
      Value zero = rewriter.create<ConstantIndexOp>(loc, 0);
      Value val =
          rewriter.create<LoadOp>(loc, operand, llvm::makeArrayRef({zero}));
      rewriter.create<linalg::GenericOp>(
          loc, /*inputs=*/ValueRange{},
          /*outputBuffers=*/ValueRange{operand_adaptor.output()},
          llvm::makeArrayRef(rewriter.getMultiDimIdentityMap(nloops)),
          GetNParallelLoopsAttrs(nloops),
          [&](OpBuilder& nestedBuilder, Location nestedLoc, ValueRange args) {
            nestedBuilder.create<linalg::YieldOp>(loc, val);
          });

    } else {
      auto indexing_maps = getIndexingMaps(op, broadcast_dims, result_shape,
                                           operand_type, &rewriter);
      rewriter.create<linalg::GenericOp>(
          loc, /*inputs=*/ValueRange{operand},
          /*outputBuffers=*/ValueRange{operand_adaptor.output()}, indexing_maps,
          GetNParallelLoopsAttrs(nloops),
          [&](OpBuilder& nestedBuilder, Location nestedLoc, ValueRange args) {
            nestedBuilder.create<linalg::YieldOp>(loc, *args.begin());
          });
    }
    rewriter.replaceOp(op, llvm::None);
    return success();
  }

  // Inserts 'linalg.reshape' if there is a size-1 dim expansion.
  std::pair<Value, SmallVector<int64_t, 2>> InsertReshapeIfNecessary(
      lmhlo::BroadcastInDimOp op, ArrayRef<Value> args,
      ConversionPatternRewriter& rewriter) const {
    lmhlo::BroadcastInDimOp::Adaptor operand_adaptor(args);
    Value operand = operand_adaptor.operand();
    auto operand_type = operand_adaptor.operand().getType().cast<MemRefType>();
    auto operand_shape = operand_type.getShape();

    Value result = operand_adaptor.output();
    auto result_type = result.getType().cast<MemRefType>();
    auto result_shape = result_type.getShape();

    SmallVector<int64_t, 2> operand_strides;
    int64_t operand_offset;
    if (failed(getStridesAndOffset(operand_type, operand_strides,
                                   operand_offset))) {
      op.emitOpError() << "Failed to get offset and strides.";
    }

    SmallVector<int64_t, 2> new_shape, new_strides, broadcast_dims;
    SmallVector<linalg::ReassociationIndices, 4> collapsed_dims_list;
    linalg::ReassociationIndices collapsed_dims;
    for (const auto& item :
         enumerate(op.broadcast_dimensions().getIntValues())) {
      size_t index = item.index();
      int dim = item.value().getSExtValue();

      collapsed_dims.push_back(index);

      bool expansion_needed =
          operand_shape[index] == 1 && result_shape[dim] != 1;
      if (expansion_needed) {
        continue;
      }
      new_shape.push_back(operand_shape[index]);
      new_strides.push_back(operand_strides[index]);
      broadcast_dims.push_back(dim);

      collapsed_dims_list.push_back(collapsed_dims);
      collapsed_dims.clear();
    }
    // If `collapsed_dims_list` is empty, then the memref has shape [1, ..., 1]
    // and all dimensions need expansion. Such memref will be reshaped to a 1D
    // memref with a single element. New shape and strides needs to be updated
    // accordingly.
    if (collapsed_dims_list.empty()) {
      collapsed_dims_list.push_back({});
      new_shape.push_back(1);
      new_strides.push_back(1);
      broadcast_dims.push_back(0);
    }
    for (const auto& dims : collapsed_dims) {
      collapsed_dims_list.back().push_back(dims);
    }

    // `linalg.reshape` is inserted only if necessary, i.e. when the rank can be
    // reduced.
    if (new_shape.size() < operand_shape.size()) {
      auto new_memref_type = MemRefType::get(
          new_shape, operand_type.getElementType(),
          makeStridedLinearLayoutMap(new_strides, operand_offset,
                                     rewriter.getContext()));
      operand = rewriter.create<linalg::ReshapeOp>(op.getLoc(), new_memref_type,
                                                   operand_adaptor.operand(),
                                                   collapsed_dims_list);
    }
    return std::make_pair(operand, broadcast_dims);
  }

  SmallVector<AffineMap, 2> getIndexingMaps(lmhlo::BroadcastInDimOp op,
                                            ArrayRef<int64_t> broadcastDims,
                                            ArrayRef<int64_t> resultShape,
                                            MemRefType operandType,
                                            Builder* b) const {
    unsigned nloops = resultShape.size();

    // The input is a scalar, i.e. this is a scalar broadcast op.
    if (operandType.getRank() == 0) {
      return {AffineMap::get(nloops, /*symbolCount=*/0, b->getContext()),
              b->getMultiDimIdentityMap(nloops)};
    }

    auto operandShape = operandType.getShape();
    SmallVector<AffineExpr, 4> dimExprs;
    dimExprs.reserve(nloops);

    for (const auto& broadcastDim : llvm::enumerate(broadcastDims)) {
      int size = broadcastDim.value();
      bool expansion_needed =
          operandShape[broadcastDim.index()] == 1 && resultShape[size] != 1;
      if (expansion_needed) {
        op.emitOpError(
            "BroadcastInDimOp lowering to Linalg does not support size-1 "
            "dimensions expansion.");
      }
      dimExprs.push_back(b->getAffineDimExpr(size));
    }
    return {
        AffineMap::get(nloops, /*symbolCount=*/0, dimExprs, b->getContext()),
        b->getMultiDimIdentityMap(nloops)};
  }
};

template <typename OpTy, bool isLHLO = true>
class TransposeConverter
    : public DataMovementOpConverter<TransposeConverter<OpTy, isLHLO>, OpTy,
                                     isLHLO> {
 public:
  using DataMovementOpConverter<TransposeConverter<OpTy, isLHLO>, OpTy,
                                isLHLO>::DataMovementOpConverter;
  static SmallVector<AffineMap, 2> getIndexingMaps(OpTy op, Builder* b) {
    auto resultType =
        getHloOpResultType<isLHLO>(op).template cast<ShapedType>();
    auto nloops = resultType.getRank();
    SmallVector<AffineExpr, 2> inputExprs;
    inputExprs.resize(resultType.getRank());
    for (auto permutation : llvm::enumerate(op.permutation())) {
      inputExprs[permutation.value().getZExtValue()] =
          b->getAffineDimExpr(permutation.index());
    }
    return {
        AffineMap::get(nloops, /*symbolCount=*/0, inputExprs, b->getContext()),
        b->getMultiDimIdentityMap(nloops)};
  }
};

// Converts reshape ops that can be proven to be either a collapse of dimensions
// or expansion of dimensions of the operand.
template <typename OpTy, bool isLHLO = true>
class ReshapeOpConverter : public OpConversionPattern<OpTy> {
 public:
  using OpConversionPattern<OpTy>::OpConversionPattern;

  LogicalResult matchAndRewrite(
      OpTy reshapeOp, ArrayRef<Value> args,
      ConversionPatternRewriter& rewriter) const final {
    if (!verifyHloOpBufferOrTensorSemantics<isLHLO>(reshapeOp))
      return failure();
    ShapedType operandType =
        reshapeOp.operand().getType().template cast<ShapedType>();
    ShapedType resultType = getHloOpResultType<isLHLO>(reshapeOp);

    if (!operandType.hasStaticShape() || !resultType.hasStaticShape())
      return failure();

    // Compute the reassociation maps for the linalg operation.
    ArrayRef<int64_t> srcShape =
        (operandType.getRank() > resultType.getRank() ? operandType.getShape()
                                                      : resultType.getShape());
    ArrayRef<int64_t> dstShape =
        (operandType.getRank() > resultType.getRank() ? resultType.getShape()
                                                      : operandType.getShape());
    unsigned currSrcDim = 0, currDstDim = 0;
    SmallVector<linalg::ReassociationExprs, 4> reassociationMap(
        dstShape.size());
    bool isExpandingOrCollapsing = true;
    while (currSrcDim < srcShape.size() && currDstDim < dstShape.size()) {
      int64_t dstSize = dstShape[currDstDim];
      int64_t srcSize = srcShape[currSrcDim];
      while (srcSize < dstSize && currSrcDim < srcShape.size()) {
        reassociationMap[currDstDim].push_back(
            rewriter.getAffineDimExpr(currSrcDim++));
        srcSize *= srcShape[currSrcDim];
      }
      if (srcSize == dstSize) {
        reassociationMap[currDstDim].push_back(
            rewriter.getAffineDimExpr(currSrcDim++));
        // If the next dim in dstShape is not 1, treat subsequent dims in
        // srcShape which are 1 to be collapsed.
        if (currDstDim == dstShape.size() - 1 ||
            dstShape[currDstDim + 1] != 1) {
          while (currSrcDim < srcShape.size() && srcShape[currSrcDim] == 1) {
            reassociationMap[currDstDim].push_back(
                rewriter.getAffineDimExpr(currSrcDim++));
          }
        }
      } else {
        isExpandingOrCollapsing = false;
        break;
      }
      currDstDim++;
    }
    if (currSrcDim != srcShape.size() || currDstDim != dstShape.size())
      isExpandingOrCollapsing = false;

    if (!isExpandingOrCollapsing) {
      auto getIdentityExprs = [&rewriter](int n) {
        SmallVector<AffineExpr, 4> exprs;
        for (int i = 0; i < n; ++i)
          exprs.push_back(rewriter.getAffineDimExpr(i));
        return exprs;
      };
      Location loc = reshapeOp.getLoc();
      int64_t totalElems = std::accumulate(srcShape.begin(), srcShape.end(), 1,
                                           std::multiplies<int64_t>());
      auto elemType = operandType.getElementType();
      SmallVector<linalg::ReassociationExprs, 4> collapsingMap = {
          getIdentityExprs(dstShape.size())};
      SmallVector<linalg::ReassociationExprs, 4> expandingMap = {
          getIdentityExprs(srcShape.size())};

      if (isLHLO) {
        auto collapsedType = MemRefType::get({totalElems}, elemType);
        Value collapsedOp = rewriter.create<linalg::ReshapeOp>(
            loc, collapsedType, args[0], collapsingMap);
        Value reshapeBuffer = rewriter.create<linalg::ReshapeOp>(
            loc, resultType, collapsedOp, expandingMap);
        rewriter.replaceOpWithNewOp<linalg::CopyOp>(
            reshapeOp, reshapeBuffer, args[1], /*inputPermutation =*/nullptr,
            /*outputPermutation =*/nullptr);
      } else {
        auto collapsedType = RankedTensorType::get({totalElems}, elemType);
        Value collapsedOp = rewriter.create<linalg::TensorReshapeOp>(
            loc, collapsedType, args[0], collapsingMap);
        rewriter.replaceOpWithNewOp<linalg::TensorReshapeOp>(
            reshapeOp, resultType, collapsedOp, expandingMap);
      }
      return success();
    }

    if (isLHLO) {
      Value reshapeBuffer = rewriter.create<linalg::ReshapeOp>(
          reshapeOp.getLoc(), resultType, args[0], reassociationMap);
      rewriter.replaceOpWithNewOp<linalg::CopyOp>(
          reshapeOp, reshapeBuffer, args[1], /*inputPermutation =*/nullptr,
          /*outputPermutation =*/nullptr);
    } else {
      rewriter.replaceOpWithNewOp<linalg::TensorReshapeOp>(
          reshapeOp, resultType, args[0], reassociationMap);
    }
    return success();
  }
};

template <typename OpTy, bool isLHLO = true>
class IotaConverter : public OpConversionPattern<OpTy> {
 public:
  using OpConversionPattern<OpTy>::OpConversionPattern;

  LogicalResult matchAndRewrite(
      OpTy iotaOp, ArrayRef<Value> args,
      ConversionPatternRewriter& rewriter) const final {
    ShapedType resultShapedType = getHloOpResultType<isLHLO>(iotaOp);
    if (!resultShapedType) return failure();

    auto resultElementType = resultShapedType.getElementType();
    if (!resultElementType.isSignlessIntOrFloat()) return failure();

    // Construct the indexing maps needed for linalg.generic ops.
    unsigned nloops = resultShapedType.getRank();

    auto linalgOp = rewriter.create<linalg::IndexedGenericOp>(
        iotaOp.getLoc(),
        /*resultTensorTypes=*/
        isLHLO ? ArrayRef<Type>{} : ArrayRef<Type>{resultShapedType},
        /*inputs=*/ValueRange{},
        /*outputBuffers=*/isLHLO ? ValueRange{args} : ValueRange{},
        /*initTensors=*/ValueRange{},
        llvm::makeArrayRef(rewriter.getMultiDimIdentityMap(nloops)),
        GetNParallelLoopsAttrs(nloops),
        [&](OpBuilder& nestedBuilder, Location nestedLoc, ValueRange ivs,
            ValueRange args) {
          Value castOp = nestedBuilder.create<IndexCastOp>(
              nestedLoc, ivs[iotaOp.iota_dimension()],
              nestedBuilder.getIntegerType(
                  resultElementType.getIntOrFloatBitWidth()));
          if (resultElementType.template isa<FloatType>()) {
            castOp = nestedBuilder.create<SIToFPOp>(nestedLoc, castOp,
                                                    resultElementType);
          }
          nestedBuilder.create<linalg::YieldOp>(nestedLoc, castOp);
        });
    if (isLHLO)
      rewriter.replaceOp(iotaOp, llvm::None);
    else
      rewriter.replaceOp(iotaOp, linalgOp.result_tensors());
    return success();
  }
};

class ConstConverter : public OpConversionPattern<lmhlo::ConstOp> {
 public:
  using OpConversionPattern<lmhlo::ConstOp>::OpConversionPattern;

  LogicalResult matchAndRewrite(
      lmhlo::ConstOp constOp, ArrayRef<Value> args,
      ConversionPatternRewriter& rewriter) const final {
    auto loc = constOp.getLoc();
    auto valueAttr = constOp.value().cast<DenseElementsAttr>();
    if (valueAttr.getType().getRank() != 0) return failure();
    auto stdConstOp =
        rewriter.create<mlir::ConstantOp>(loc, valueAttr.getValue({}));
    rewriter.create<mlir::AffineStoreOp>(loc, stdConstOp, constOp.getOperand(),
                                         ValueRange());
    rewriter.eraseOp(constOp);
    return success();
  }
};

// TODO(b/156787842): Support the lowering for dynamic shapes.
template <typename OpTy, bool isLHLO = true>
class ReverseConverter
    : public DataMovementOpConverter<ReverseConverter<OpTy, isLHLO>, OpTy,
                                     isLHLO> {
 public:
  using DataMovementOpConverter<ReverseConverter<OpTy, isLHLO>, OpTy,
                                isLHLO>::DataMovementOpConverter;
  static SmallVector<AffineMap, 2> getIndexingMaps(OpTy op, Builder* b) {
    auto resultType =
        getHloOpResultType<isLHLO>(op).template cast<ShapedType>();
    auto nloops = resultType.getRank();
    SmallVector<AffineExpr, 2> inputExprs;
    inputExprs.reserve(nloops);
    for (int i = 0; i < nloops; ++i)
      inputExprs.push_back(b->getAffineDimExpr(i));
    for (auto dim : op.dimensions()) {
      int i = dim.getZExtValue();
      if (resultType.isDynamicDim(i)) return {};
      int n = resultType.getShape()[i];
      inputExprs[i] = b->getAffineConstantExpr(n - 1) - inputExprs[i];
    }
    return {
        AffineMap::get(nloops, /*symbolCount=*/0, inputExprs, b->getContext()),
        b->getMultiDimIdentityMap(nloops)};
  }
};

class SliceConverter : public OpConversionPattern<lmhlo::SliceOp> {
 public:
  using OpConversionPattern<lmhlo::SliceOp>::OpConversionPattern;

  LogicalResult matchAndRewrite(
      lmhlo::SliceOp sliceOp, ArrayRef<Value> args,
      ConversionPatternRewriter& rewriter) const final {
    auto loc = sliceOp.getLoc();
    auto argType =
        sliceOp.getOperand(0).getType().template dyn_cast<ShapedType>();
    if (!argType || !argType.hasRank()) {
      emitError(loc, "lhlo to linalg conversion expects known-rank args");
      return failure();
    }

    SmallVector<Value, 3> ranges;
    for (int i = 0, e = argType.getRank(); i < e; ++i) {
      Value start_index = rewriter.create<ConstantIndexOp>(
          loc, sliceOp.start_indices().getValue<int64_t>(i));
      Value limit_index = rewriter.create<ConstantIndexOp>(
          loc, sliceOp.limit_indices().getValue<int64_t>(i));
      Value stride = rewriter.create<ConstantIndexOp>(
          loc, sliceOp.strides().getValue<int64_t>(i));
      ranges.push_back(rewriter.create<linalg::RangeOp>(loc, start_index,
                                                        limit_index, stride));
    }
    auto linalg_slice =
        rewriter.create<linalg::SliceOp>(loc, sliceOp.getOperand(0), ranges);
    rewriter.create<linalg::CopyOp>(loc, linalg_slice, sliceOp.getOperand(1));
    rewriter.eraseOp(sliceOp);
    return success();
  }
};

void populateLHLOToLinalgConversionPattern(MLIRContext* context,
                                           OwningRewritePatternList* patterns) {
  // clang-format off
  patterns->insert<BroadcastConverter<lmhlo::BroadcastOp>,
                   ConstConverter,
                   ConvToLinalgConverter,
                   IotaConverter<lmhlo::IotaOp>,
                   LhloBroadcastInDimConverter,
                   PointwiseToLinalgConverter<lmhlo::AbsOp>,
                   PointwiseToLinalgConverter<lmhlo::AddOp>,
                   PointwiseToLinalgConverter<lmhlo::AndOp>,
                   PointwiseToLinalgConverter<lmhlo::Atan2Op>,
                   PointwiseToLinalgConverter<lmhlo::CeilOp>,
                   PointwiseToLinalgConverter<lmhlo::CompareOp>,
                   PointwiseToLinalgConverter<lmhlo::ComplexOp>,
                   PointwiseToLinalgConverter<lmhlo::ConvertOp>,
                   // TODO(ataei): Remove this pattern, CopyOp is folded away.
                   PointwiseToLinalgConverter<lmhlo::CopyOp>,
                   PointwiseToLinalgConverter<lmhlo::CosOp>,
                   PointwiseToLinalgConverter<lmhlo::DivOp>,
                   PointwiseToLinalgConverter<lmhlo::ExpOp>,
                   PointwiseToLinalgConverter<lmhlo::FloorOp>,
                   PointwiseToLinalgConverter<lmhlo::ImagOp>,
                   PointwiseToLinalgConverter<lmhlo::LogOp>,
                   PointwiseToLinalgConverter<lmhlo::MaxOp>,
                   PointwiseToLinalgConverter<lmhlo::MinOp>,
                   PointwiseToLinalgConverter<lmhlo::MulOp>,
                   PointwiseToLinalgConverter<lmhlo::NegOp>,
                   PointwiseToLinalgConverter<lmhlo::NotOp>,
                   PointwiseToLinalgConverter<lmhlo::RealOp>,
                   PointwiseToLinalgConverter<lmhlo::RemOp>,
                   PointwiseToLinalgConverter<lmhlo::RsqrtOp>,
                   PointwiseToLinalgConverter<lmhlo::SelectOp>,
                   PointwiseToLinalgConverter<lmhlo::SignOp>,
                   PointwiseToLinalgConverter<lmhlo::SinOp>,
                   PointwiseToLinalgConverter<lmhlo::SqrtOp>,
                   PointwiseToLinalgConverter<lmhlo::SubOp>,
                   PointwiseToLinalgConverter<lmhlo::TanhOp>,
                   PointwiseToLinalgConverter<lmhlo::IsFiniteOp>,
                   ReshapeOpConverter<lmhlo::ReshapeOp>,
                   ReverseConverter<lmhlo::ReverseOp>,
                   ScalarPointwiseToStandardConverter<lmhlo::AddOp>,
                   SliceConverter,
                   TransposeConverter<lmhlo::TransposeOp>
                  >(context);
  // clang-format on
}

// Converts LHLO ops to Linalg generic.
// Sample result for lmhlo::AddOp.
//
// "lmhlo.add"(%arg1, %arg2, %out) :
//      (memref<2x2xf32>, memref<2x2xf32>, memref<2x2xf32>) -> ()
//
// will be converted to
//
// #map0 = (d0, d1) -> (d0, d1)
// "linalg.generic"(%arg1, %arg2, %out) ( {
//   ^bb0(%arg4: f32, %arg5: f32):
//     %0 = addf %arg4, %arg5 : f32
//     "linalg.yield"(%0) : (f32) -> ()
// }) {
//     indexing_maps = [#map0, #map0, #map0],
//     iterator_types = ["parallel", "parallel"],
// } : (memref<2x2xf32>, memref<2x2xf32>, memref<2x2xf32>) -> ()
struct LhloLegalizeToLinalgPass
    : public PassWrapper<LhloLegalizeToLinalgPass, FunctionPass> {
  void getDependentDialects(DialectRegistry& registry) const override {
    registry.insert<AffineDialect, linalg::LinalgDialect>();
  }

  void runOnFunction() override {
    OwningRewritePatternList patterns;
    ConversionTarget target(getContext());
    target.addLegalDialect<linalg::LinalgDialect, StandardOpsDialect,
                           AffineDialect>();

    auto func = getFunction();
    populateLHLOToLinalgConversionPattern(func.getContext(), &patterns);
    if (failed(applyPartialConversion(func, target, patterns, nullptr))) {
      signalPassFailure();
    }
  }
};

struct HloLegalizeToLinalgPass
    : public PassWrapper<HloLegalizeToLinalgPass, FunctionPass> {
  void getDependentDialects(DialectRegistry& registry) const override {
    registry.insert<linalg::LinalgDialect>();
  }

  void runOnFunction() override {
    OwningRewritePatternList patterns;
    ConversionTarget target(getContext());
    target.addLegalDialect<linalg::LinalgDialect, StandardOpsDialect>();

    auto func = getFunction();
    mhlo::populateHLOToLinalgConversionPattern(func.getContext(), &patterns);
    if (failed(applyPartialConversion(func, target, patterns, nullptr))) {
      signalPassFailure();
    }
  }
};

}  // namespace

namespace lmhlo {
std::unique_ptr<OperationPass<FuncOp>> createLegalizeLhloToLinalgPass() {
  return std::make_unique<LhloLegalizeToLinalgPass>();
}
}  // namespace lmhlo

namespace mhlo {

void populateHLOToLinalgConversionPattern(MLIRContext* context,
                                          OwningRewritePatternList* patterns) {
  patterns
      ->insert<BroadcastConverter<mhlo::BroadcastOp, false>,
               HloBroadcastInDimConverter, IotaConverter<mhlo::IotaOp, false>,
               PointwiseToLinalgConverter<mhlo::AbsOp, false>,
               PointwiseToLinalgConverter<mhlo::AddOp, false>,
               PointwiseToLinalgConverter<mhlo::AndOp, false>,
               PointwiseToLinalgConverter<mhlo::Atan2Op, false>,
               PointwiseToLinalgConverter<mhlo::CeilOp, false>,
               PointwiseToLinalgConverter<mhlo::CompareOp, false>,
               PointwiseToLinalgConverter<mhlo::ComplexOp, false>,
               PointwiseToLinalgConverter<mhlo::ConvertOp, false>,
               PointwiseToLinalgConverter<mhlo::CopyOp, false>,
               PointwiseToLinalgConverter<mhlo::CosOp, false>,
               PointwiseToLinalgConverter<mhlo::DivOp, false>,
               PointwiseToLinalgConverter<mhlo::ExpOp, false>,
               PointwiseToLinalgConverter<mhlo::FloorOp, false>,
               PointwiseToLinalgConverter<mhlo::ImagOp, false>,
               PointwiseToLinalgConverter<mhlo::LogOp, false>,
               PointwiseToLinalgConverter<mhlo::MaxOp, false>,
               PointwiseToLinalgConverter<mhlo::MinOp, false>,
               PointwiseToLinalgConverter<mhlo::MulOp, false>,
               PointwiseToLinalgConverter<mhlo::NegOp, false>,
               PointwiseToLinalgConverter<mhlo::NotOp, false>,
               PointwiseToLinalgConverter<mhlo::RealOp, false>,
               PointwiseToLinalgConverter<mhlo::RemOp, false>,
               PointwiseToLinalgConverter<mhlo::RsqrtOp, false>,
               PointwiseToLinalgConverter<mhlo::SelectOp, false>,
               PointwiseToLinalgConverter<mhlo::SinOp, false>,
               PointwiseToLinalgConverter<mhlo::SqrtOp, false>,
               PointwiseToLinalgConverter<mhlo::SubOp, false>,
               PointwiseToLinalgConverter<mhlo::TanhOp, false>,
               PointwiseToLinalgConverter<mhlo::IsFiniteOp, false>,
               ReshapeOpConverter<mhlo::ReshapeOp, false>,
               ReverseConverter<mhlo::ReverseOp, false>,
               TransposeConverter<mhlo::TransposeOp, false>>(context);
}

std::unique_ptr<OperationPass<FuncOp>> createLegalizeHloToLinalgPass() {
  return std::make_unique<HloLegalizeToLinalgPass>();
}
}  // namespace mhlo
}  // namespace mlir
