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

// Thsi file implements passes to convert complex operations to equivalent real
// value operations. This does not include removing complex values from function
// argument or return types.

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <numeric>

#include "llvm/ADT/STLExtras.h"
#include "mlir-hlo/Dialect/mhlo/IR/hlo_ops.h"
#include "mlir-hlo/Dialect/mhlo/transforms/passes.h"
#include "mlir-hlo/utils/hlo_utils.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/TypeUtilities.h"
#include "mlir/IR/Types.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"

using mlir::FunctionPass;
using mlir::OwningRewritePatternList;
using mlir::PassWrapper;

namespace {
class LowerComplexPass : public PassWrapper<LowerComplexPass, FunctionPass> {
 public:
  explicit LowerComplexPass() : PassWrapper<LowerComplexPass, FunctionPass>() {}

  /// Performs the lowering to MHLO dialect.
  void runOnFunction() override;
};
}  // end anonymous namespace

namespace mlir {
namespace mhlo {
namespace {

#include "generated_lower_complex.inc"

}  // end anonymous namespace

void PopulateComplexLoweringPatterns(MLIRContext* context,
                                     OwningRewritePatternList* patterns) {
  populateWithGenerated(context, *patterns);
}
}  // end namespace mhlo
}  // end namespace mlir

// Lowers the complex operations that can be represented using other operations.
void LowerComplexPass::runOnFunction() {
  // Add lowering patterns to the list.
  OwningRewritePatternList patterns;
  mlir::mhlo::PopulateComplexLoweringPatterns(&getContext(), &patterns);

  applyPatternsAndFoldGreedily(getFunction(), patterns);
}

std::unique_ptr<FunctionPass> mlir::mhlo::createLowerComplexPass() {
  return std::make_unique<LowerComplexPass>();
}
