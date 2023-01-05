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

#include "mlir/Dialect/StandardOps/IR/Ops.h"  // from @llvm-project
#include "mlir/Pass/Pass.h"  // from @llvm-project
#include "mlir/Transforms/DialectConversion.h"  // from @llvm-project
#include "tensorflow/compiler/mlir/tools/kernel_gen/ir/tf_framework_ops.h"
#include "tensorflow/compiler/mlir/tools/kernel_gen/transforms/passes.h"
#include "tensorflow/compiler/mlir/tools/kernel_gen/transforms/rewriters.h"

namespace mlir {
namespace kernel_gen {
namespace tf_framework {
namespace {

#define GEN_PASS_CLASSES
#include "tensorflow/compiler/mlir/tools/kernel_gen/transforms/kernel_gen_passes.h.inc"

static constexpr StringRef kTFEntry = "tf_entry";

// The pass rewrites the function marked with `tf_entry` attribute.
// * adds tf_framework::OpKernelContextType argument to the function,
// * std.alloc becomes tf_framework.alloc_raw,
// * std.dealloc becomes tf_framework.dealloc_raw.
class EmbedTFFrameworkPass
    : public EmbedTFFrameworkPassBase<EmbedTFFrameworkPass> {
  void getDependentDialects(DialectRegistry& registry) const override {
    registry.insert<mlir::kernel_gen::tf_framework::TFFrameworkDialect>();
  }

 public:
  void runOnOperation() override {
    ModuleOp m = getOperation();

    // Populate patterns.
    OwningRewritePatternList patterns;
    PopulateEmbedTFFrameworkConversionPatterns(m.getContext(), &patterns);

    // Set target.
    ConversionTarget target(getContext());
    target.addLegalDialect<tf_framework::TFFrameworkDialect>();

    target.addDynamicallyLegalOp<FuncOp>([&](FuncOp op) {
      if (!op.getAttrOfType<UnitAttr>(kTFEntry)) {
        return true;
      }
      FunctionType func_type = op.getType();
      return func_type.getNumInputs() > 0 &&
             func_type.getInput(0).isa<OpKernelContextType>();
    });
    target.addDynamicallyLegalOp<AllocOp, DeallocOp>([](Operation* op) {
      return !op->getParentOfType<FuncOp>().getAttrOfType<UnitAttr>(kTFEntry);
    });

    if (failed(applyPartialConversion(m, target, patterns))) {
      signalPassFailure();
    }
  }
};

}  // namespace

std::unique_ptr<OperationPass<ModuleOp> > CreateEmbedTFFrameworkPass() {
  return std::make_unique<EmbedTFFrameworkPass>();
}

}  // namespace tf_framework
}  // namespace kernel_gen
}  // namespace mlir
