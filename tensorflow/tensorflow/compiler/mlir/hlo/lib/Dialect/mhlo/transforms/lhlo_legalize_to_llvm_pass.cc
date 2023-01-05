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

#include "mlir-hlo/Dialect/mhlo/IR/lhlo_ops.h"
#include "mlir-hlo/Dialect/mhlo/transforms/rewriters.h"
#include "mlir/Conversion/StandardToLLVM/ConvertStandardToLLVM.h"
#include "mlir/Conversion/StandardToLLVM/ConvertStandardToLLVMPass.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/IR/StandardTypes.h"
#include "mlir/Pass/Pass.h"

namespace mlir {
namespace lmhlo {
namespace {

class TestLhloToLLVMPass
    : public ::mlir::PassWrapper<TestLhloToLLVMPass,
                                 ::mlir::OperationPass<::mlir::ModuleOp>> {
  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<LLVM::LLVMDialect>();
  }

 public:
  void runOnOperation() override {
    ModuleOp m = getOperation();

    OwningRewritePatternList patterns;
    LLVMTypeConverter converter(&getContext());
    populateStdToLLVMConversionPatterns(converter, patterns);
    PopulateLhloToLLVMConversionPatterns(&converter, &patterns);

    ConversionTarget target(getContext());
    target.addLegalDialect<LLVM::LLVMDialect>();
    target.addLegalOp<ModuleOp, ModuleTerminatorOp>();
    target.addIllegalDialect<LmhloDialect>();

    if (failed(applyFullConversion(m, target, patterns))) {
      signalPassFailure();
    }
  }
};

}  // namespace

std::unique_ptr<Pass> createTestLhloToLLVMPass() {
  return std::make_unique<TestLhloToLLVMPass>();
}

}  // namespace lmhlo
}  // namespace mlir
