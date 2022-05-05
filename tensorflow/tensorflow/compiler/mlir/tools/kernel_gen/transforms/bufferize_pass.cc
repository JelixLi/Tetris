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

// This file implements logic for translating mixed IR to buffer form.
// Currently it supports MHLO and some operations from the Standard dialect.

#include <memory>

#include "mlir/Dialect/SCF/SCF.h"  // from @llvm-project
#include "mlir/Dialect/Shape/IR/Shape.h"  // from @llvm-project
#include "mlir/Dialect/Shape/Transforms/Passes.h"  // from @llvm-project
#include "mlir/Dialect/StandardOps/IR/Ops.h"  // from @llvm-project
#include "mlir/IR/Function.h"  // from @llvm-project
#include "mlir/IR/MLIRContext.h"  // from @llvm-project
#include "mlir/IR/Operation.h"  // from @llvm-project
#include "mlir/IR/PatternMatch.h"  // from @llvm-project
#include "mlir/IR/StandardTypes.h"  // from @llvm-project
#include "mlir/IR/Visitors.h"  // from @llvm-project
#include "mlir/Transforms/Bufferize.h"  // from @llvm-project
#include "mlir/Transforms/DialectConversion.h"  // from @llvm-project
#include "tensorflow/compiler/mlir/hlo/include/mlir-hlo/Dialect/mhlo/IR/hlo_ops.h"
#include "tensorflow/compiler/mlir/hlo/include/mlir-hlo/Dialect/mhlo/IR/lhlo_ops.h"
#include "tensorflow/compiler/mlir/hlo/include/mlir-hlo/Dialect/mhlo/transforms/passes.h"
#include "tensorflow/compiler/mlir/hlo/include/mlir-hlo/Dialect/mhlo/transforms/rewriters.h"
#include "tensorflow/compiler/mlir/tools/kernel_gen/transforms/passes.h"
#include "tensorflow/compiler/mlir/tools/kernel_gen/transforms/rewriters.h"

namespace mlir {
namespace kernel_gen {
namespace transforms {
namespace {

#define GEN_PASS_CLASSES
#include "tensorflow/compiler/mlir/tools/kernel_gen/transforms/kernel_gen_passes.h.inc"

// TODO(herhut) : This could become a real pattern in bufferize pass. What we
// would need to do is insert a copy to model the semantics correctly. The same
// is true for the TensorLoad pattern that is already in there.  Then buffer
// assignment free insertion and copy removal should clean this up for us.
//
// This patten erases `tensor_store(src_unranked_tensor, dst_unranked_memref)`
// op and replaces the result of the defining op produced `dst_unranked_memref`
// with the rewritten `src_unranked_tensor`.
class UnrankedTensorStoreTestOnlyPattern
    : public OpConversionPattern<mlir::TensorStoreOp> {
 public:
  using OpConversionPattern<mlir::TensorStoreOp>::OpConversionPattern;

  LogicalResult matchAndRewrite(
      mlir::TensorStoreOp op, ArrayRef<Value> operands,
      ConversionPatternRewriter& rewriter) const final {
    rewriter.replaceOp(op.memref().getDefiningOp(), op.tensor());
    rewriter.replaceOp(op, {});
    return success();
  }
};

struct BufferizePass : public BufferizePassBase<BufferizePass> {
  void getDependentDialects(DialectRegistry& registry) const override {
    registry.insert<lmhlo::LmhloDialect>();
  }

 public:
  void runOnOperation() override {
    auto& context = getContext();
    ConversionTarget target(context);
    target.addLegalDialect<lmhlo::LmhloDialect, scf::SCFDialect,
                           StandardOpsDialect>();
    target.addLegalOp<ModuleOp, ModuleTerminatorOp>();
    target.addIllegalDialect<mhlo::MhloDialect>();
    target.addIllegalOp<DynamicTensorFromElementsOp, ExtractElementOp,
                        TensorFromElementsOp, TensorLoadOp, TensorCastOp>();
    target.addDynamicallyLegalOp<TensorStoreOp>([&](TensorStoreOp op) {
      return !op.tensor().getType().isa<UnrankedTensorType>();
    });

    BufferizeTypeConverter converter;
    auto typesAreLegal = [&converter](Operation* op) {
      return converter.isLegal(op->getOperandTypes()) &&
             converter.isLegal(op->getResultTypes());
    };
    target.addDynamicallyLegalOp<FuncOp>([&](FuncOp op) {
      auto inputs = op.getType().getInputs();
      auto results = op.getType().getResults();
      return converter.isLegal(inputs) && converter.isLegal(results) &&
             converter.isLegal(&op.getBody());
    });
    target.addDynamicallyLegalOp<CallOp, ReturnOp, SelectOp, shape::AssumingOp>(
        typesAreLegal);

    OwningRewritePatternList patterns;
    mhlo::populateHLOToLHLOConversionPattern(&context, &converter, &patterns);
    populateWithBufferizeOpConversionPatterns<ReturnOp, ReturnOp,
                                              lmhlo::CopyOp>(
        &context, converter, patterns);
    populateStandardBufferizePattern(&context, &converter, &patterns);
    populateShapeTypeConversionPatterns(&context, converter, patterns);
    patterns.insert<UnrankedTensorStoreTestOnlyPattern>(&context);

    auto module = getOperation();
    if (failed(applyPartialConversion(module, target, patterns))) {
      signalPassFailure();
    }
  }
};

}  // namespace

std::unique_ptr<OperationPass<ModuleOp> > CreateBufferizePass() {
  return std::make_unique<BufferizePass>();
}

}  // namespace transforms
}  // namespace kernel_gen
}  // namespace mlir
