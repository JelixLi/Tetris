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

#include "tensorflow/compiler/mlir/tools/kernel_gen/tf_framework_c_interface.h"

#include "tensorflow/core/framework/allocator.h"
#include "tensorflow/core/framework/op_kernel.h"

namespace mlir {
namespace kernel_gen {
namespace tf_framework {
namespace {

using tensorflow::Allocator;
using tensorflow::AllocatorAttributes;

Allocator* GetAllocator(void* op_kernel_ctx) {
  auto* ctx = static_cast<tensorflow::OpKernelContext*>(op_kernel_ctx);
  // TODO(pifon): Figure out how to set AllocatorAttributes correctly.
  AllocatorAttributes attrs;
  return ctx->get_allocator(attrs);
}

}  // namespace

extern "C" void* _mlir_ciface_tf_alloc(void* op_kernel_ctx, size_t num_bytes,
                                       int32_t output_index,
                                       int32_t num_candidates,
                                       int32_t* candidate_input_indices) {
  auto* ctx = static_cast<tensorflow::OpKernelContext*>(op_kernel_ctx);

  if (output_index != -1) {
    auto element_size = ctx->expected_output_dtype(output_index);
    // Create a 1D shape, because the shapes don't have to match exactly for
    // input forwarding. Only the number of elements must be the same.
    tensorflow::TensorShape output_shape;
    output_shape.AddDim(num_bytes / element_size);

    // Iterate over indices of all inputs that can potentially be used for
    // forwarding.
    for (int i = 0; i < num_candidates; ++i) {
      // TODO(pifon): Expose fetching AllocatorAttributes with the output_index.
      AllocatorAttributes output_attr;
      auto tensor = ctx->forward_input(
          candidate_input_indices[i], output_index, element_size, output_shape,
          ctx->output_memory_type(output_index), output_attr);
      if (tensor != nullptr) {
        return tensor->data();
      }
    }
  }
  // If no forwarding happened, allocate a chunk of memory.
  return GetAllocator(op_kernel_ctx)
      ->AllocateRaw(Allocator::kAllocatorAlignment, num_bytes);
}

extern "C" void _mlir_ciface_tf_dealloc(void* op_kernel_ctx, void* ptr) {
  GetAllocator(op_kernel_ctx)->DeallocateRaw(ptr);
}

}  // namespace tf_framework
}  // namespace kernel_gen
}  // namespace mlir
