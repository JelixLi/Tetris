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

#ifndef TENSORFLOW_CORE_KERNELS_MLIR_GENERATED_UNRANKED_OP_GPU_ABS_H_
#define TENSORFLOW_CORE_KERNELS_MLIR_GENERATED_UNRANKED_OP_GPU_ABS_H_

#include "mlir/ExecutionEngine/CRunnerUtils.h"  // from @llvm-project
#include "tensorflow/core/framework/op_kernel.h"

namespace tensorflow {

// Returns a pointer to an allocated MlirTensorBuffer that takes ownership of
// pre-allocated memory.
TensorBuffer* GetMlirTensorBuffer(const void* ptr, size_t size,
                                  Allocator* allocator);

template <typename ElemType>
::UnrankedMemRefType<ElemType> ConvertTensorToDescriptor(const Tensor& tensor) {
  ::UnrankedMemRefType<ElemType> result;
  result.rank = tensor.dims();
  result.descriptor = malloc(sizeof(void*) * (2 * result.rank + 3));

  // Fill the descriptor.
  void** pointers = static_cast<void**>(result.descriptor);
  pointers[0] = tensor.data();
  pointers[1] = tensor.data();
  intptr_t* int_pointers = static_cast<intptr_t*>(result.descriptor);
  int_pointers[2] = 0;
  // Fill size.
  for (int i = 0; i < result.rank; ++i) {
    int_pointers[3 + i] = tensor.dim_size(i);
  }
  // Fill strides.
  int64_t stride = 1;
  for (int i = result.rank - 1; i >= 0; --i) {
    int_pointers[i + result.rank + 3] = stride;
    stride *= tensor.dim_size(i);
  }
  return result;
}

template <typename ElemType>
Tensor ConvertDescriptorToTensor(
    ::UnrankedMemRefType<ElemType> unranked_descriptor, DataType tf_data_type,
    Allocator* allocator) {
  void* base_ptr = static_cast<void**>(unranked_descriptor.descriptor)[0];
  TensorShape result_shape;
  intptr_t* pointers = static_cast<intptr_t*>(unranked_descriptor.descriptor);
  for (int i = 0; i < unranked_descriptor.rank; ++i) {
    result_shape.AddDim(pointers[3 + i]);
  }
  TensorBuffer* buffer = GetMlirTensorBuffer(
      base_ptr, sizeof(ElemType) * result_shape.num_elements(), allocator);

  // Tensor takes ownership of the buffer.
  Tensor tensor{tf_data_type, result_shape, buffer};
  // When Tensor is constructed, its ref-counter is incremented. We need to
  // decrement it back.
  buffer->Unref();
  return tensor;
}

#define MLIR_FUNCTION(tf_op, mlir_type) _mlir_ciface_##tf_op##_##mlir_type

// Generates a class derived from OpKernel with Compute function that converts
// input tensors to unranked memref descriptors and calls mlir-generated
// unranked kernel. The outputs are converted back to tensors using
// MlirTensorBuffer to take ownership of pre-allocated memory.
#define REGISTER_AND_GENERATE_KERNEL(tf_op, mlir_type, tf_data_type,          \
                                     data_type)                               \
  extern "C" ::UnrankedMemRefType<data_type> MLIR_FUNCTION(tf_op, mlir_type)( \
      tensorflow::OpKernelContext * ctx,                                      \
      ::UnrankedMemRefType<data_type> * arg);                                 \
                                                                              \
  namespace {                                                                 \
  class MlirUnranked##tf_op##mlir_type##Op : public OpKernel {                \
   public:                                                                    \
    MlirUnranked##tf_op##mlir_type##Op(OpKernelConstruction* ctx)             \
        : OpKernel(ctx) {}                                                    \
                                                                              \
    void Compute(OpKernelContext* ctx) override {                             \
      const Tensor& input = ctx->input(0);                                    \
                                                                              \
      auto input_desc = ConvertTensorToDescriptor<data_type>(input);          \
      auto result_desc = MLIR_FUNCTION(tf_op, mlir_type)(ctx, &input_desc);   \
      free(input_desc.descriptor);                                            \
                                                                              \
      tensorflow::AllocatorAttributes attrs;                                  \
      auto* allocator = ctx->get_allocator(attrs);                            \
                                                                              \
      Tensor result_tensor = ConvertDescriptorToTensor<data_type>(            \
          result_desc, tf_data_type, allocator);                              \
      free(result_desc.descriptor);                                           \
      ctx->set_output(0, result_tensor);                                      \
    }                                                                         \
  };                                                                          \
  }                                                                           \
                                                                              \
  REGISTER_KERNEL_BUILDER(                                                    \
      Name(#tf_op).Device(DEVICE_GPU).TypeConstraint<data_type>("T"),         \
      MlirUnranked##tf_op##mlir_type##Op);

}  // namespace tensorflow

#endif  // TENSORFLOW_CORE_KERNELS_MLIR_GENERATED_UNRANKED_OP_GPU_ABS_H_
