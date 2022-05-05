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

#ifndef TENSORFLOW_COMPILER_MLIR_TOOLS_KERNEL_GEN_TRANSFORMS_PASSES_H_
#define TENSORFLOW_COMPILER_MLIR_TOOLS_KERNEL_GEN_TRANSFORMS_PASSES_H_

#include <memory>

#include "mlir/Dialect/GPU/GPUDialect.h"  // from @llvm-project
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"  // from @llvm-project
#include "mlir/IR/Module.h"  // from @llvm-project
#include "mlir/Pass/Pass.h"  // from @llvm-project

namespace mlir {
namespace kernel_gen {
namespace tf_framework {

// Pass to replace some of the Standard ops with TF Framework ops.
// * adds tf_framework::OpKernelContextType argument to the function
// * std.alloc becomes tf_framework.alloc_raw
// * std.dealloc becomes tf_framework.dealloc_raw
std::unique_ptr<OperationPass<ModuleOp> > CreateEmbedTFFrameworkPass();

}  // namespace tf_framework

namespace transforms {

// Pass for applying LLVM legalization patterns.
std::unique_ptr<OperationPass<ModuleOp> > CreateTFKernelToLLVMPass();

// Pass to tranform shape computations in shape dialect to standard and scf
// using memref descriptors.
std::unique_ptr<OperationPass<ModuleOp> > CreateShapeToDescriptorsPass();

// Pass to tranform computations on values to their corresponding parts on
// buffers.
std::unique_ptr<OperationPass<ModuleOp> > CreateBufferizePass();

// Pass to materialize broadcasts.
std::unique_ptr<FunctionPass> CreateMaterializeBroadcastsPass();

// Pass to convert scf::ParallelOp to scf::ForOp.
std::unique_ptr<FunctionPass> CreateParallelLoopsToSequential();

// Pass to propagate TF ABI knowledge, e.g. offsets, alignment.
std::unique_ptr<OperationPass<LLVM::LLVMFuncOp>>
CreatePropagateTensorFlowABIKnowledgePass(
    llvm::ArrayRef<uint32_t> same_shape = {});

// Pass to annotate GPU Module with its PTX.
std::unique_ptr<OperationPass<gpu::GPUModuleOp>> CreateGpuKernelToBlobPass(
    mlir::StringRef blob_annotation = "",
    ArrayRef<std::string> architectures = {}, bool generate_fatbin = true);

// Pass to unfuse batch norm.
std::unique_ptr<FunctionPass> CreateUnfuseBatchNormPass();

}  // namespace transforms

#define GEN_PASS_REGISTRATION
#include "tensorflow/compiler/mlir/tools/kernel_gen/transforms/kernel_gen_passes.h.inc"

}  // namespace kernel_gen
}  // namespace mlir

#endif  // TENSORFLOW_COMPILER_MLIR_TOOLS_KERNEL_GEN_TRANSFORMS_PASSES_H_
