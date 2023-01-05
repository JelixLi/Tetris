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

//===- kernel_creator.h -----------------------------------------*- C++ -*-===//
//
// This file declares the function to compile a TF kernel function to gpu
// binary (hsaco for AMD, cubin for NVIDIA) or to a gpu binary with host side.
//
//===----------------------------------------------------------------------===//
#ifndef TENSORFLOW_COMPILER_MLIR_TOOLS_KERNEL_GEN_KERNEL_CREATOR_H_
#define TENSORFLOW_COMPILER_MLIR_TOOLS_KERNEL_GEN_KERNEL_CREATOR_H_

#include <utility>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "mlir/IR/MLIRContext.h"  // from @llvm-project
#include "mlir/IR/Module.h"  // from @llvm-project
#include "tensorflow/compiler/xla/statusor.h"

namespace tensorflow {
namespace kernel_gen {

// Converts TF code to LLVM/NVVM. If `gpu_binary_only` is true, then the
// conversion stops after gpu_binary blob is generated. If `gpu_binary_only` is
// false, lowers the host side to LLVM Dialect.
xla::StatusOr<mlir::OwningModuleRef> GenerateKernelForTfCode(
    mlir::MLIRContext& context, llvm::StringRef tf_code, bool gpu_binary_only,
    llvm::ArrayRef<std::string> architectures = {"sm_75"},
    llvm::ArrayRef<uint32_t> tile_sizes = {16, 64},
    llvm::ArrayRef<uint32_t> same_shape = {},
    llvm::ArrayRef<uint32_t> unroll_factors = {}, bool generate_fatbin = true);

// Extracts gpu_binary from the converted module.
xla::StatusOr<std::string> ExtractGpuBinary(mlir::ModuleOp module);

}  // namespace kernel_gen
}  // namespace tensorflow

#endif  // TENSORFLOW_COMPILER_MLIR_TOOLS_KERNEL_GEN_KERNEL_CREATOR_H_
