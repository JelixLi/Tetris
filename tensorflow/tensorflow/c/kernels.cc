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

#include "tensorflow/c/kernels.h"

#include <memory>

#include "tensorflow/c/c_api_internal.h"
#include "tensorflow/c/tf_status_helper.h"
#include "tensorflow/c/tf_tensor_internal.h"
#include "tensorflow/core/framework/kernel_def_builder.h"
#include "tensorflow/core/framework/op_kernel.h"
#include "tensorflow/core/framework/register_types.h"
#include "tensorflow/core/framework/types.h"
#include "tensorflow/core/platform/types.h"

// This file forms the basis of a stable ABI for third-party kernel
// implementations. It is crucial that changes to this file are made cautiously
// and with a focus on maintaining both source and binary compatibility.

struct TF_KernelBuilder {
  ::tensorflow::KernelDefBuilder* cc_builder;

  void* (*create_function)(TF_OpKernelConstruction*);
  void (*compute_function)(void*, TF_OpKernelContext*);
  void (*delete_function)(void*);
};

TF_KernelBuilder* TF_NewKernelBuilder(
    const char* op_name, const char* device_name,
    void* (*create_func)(TF_OpKernelConstruction*),
    void (*compute_func)(void*, TF_OpKernelContext*),
    void (*delete_func)(void*)) {
  TF_KernelBuilder* result = new TF_KernelBuilder;
  result->cc_builder = new ::tensorflow::KernelDefBuilder(op_name);
  result->cc_builder->Device(device_name);
  result->create_function = create_func;
  result->compute_function = compute_func;
  result->delete_function = delete_func;
  return result;
}

void TF_DeleteKernelBuilder(TF_KernelBuilder* builder) {
  if (builder != nullptr) {
    delete builder->cc_builder;
    delete builder;
  }
}

namespace tensorflow {
namespace {

#define CASE(type)                                               \
  case DataTypeToEnum<type>::value: {                            \
    kernel_builder->cc_builder->TypeConstraint<type>(attr_name); \
    break;                                                       \
  }

void AddTypeConstraint(TF_KernelBuilder* kernel_builder, const char* attr_name,
                       const DataType dtype, TF_Status* status) {
  // This needs to be under tensorflow:: namespace so that
  // TF_CALL_ALL_TYPES macro can find tensorflow::string as string.
  switch (dtype) {
    TF_CALL_ALL_TYPES(CASE);
    default:
      status->status = errors::Unimplemented("Unexpected type ", dtype);
      return;
  }
  TF_SetStatus(status, TF_OK, "");
}
#undef CASE
}  // namespace
}  // namespace tensorflow

void TF_KernelBuilder_TypeConstraint(TF_KernelBuilder* kernel_builder,
                                     const char* attr_name,
                                     const TF_DataType type,
                                     TF_Status* status) {
  tensorflow::DataType dtype = static_cast<tensorflow::DataType>(type);
  tensorflow::AddTypeConstraint(kernel_builder, attr_name, dtype, status);
}

void TF_KernelBuilder_HostMemory(TF_KernelBuilder* kernel_builder,
                                 const char* arg_name) {
  kernel_builder->cc_builder->HostMemory(arg_name);
}

void TF_KernelBuilder_Priority(TF_KernelBuilder* kernel_builder,
                               int32_t priority_number) {
  kernel_builder->cc_builder->Priority(priority_number);
}

namespace tensorflow {
namespace {

// An OpKernel whose methods delegate to C function pointers.
class COpKernel : public OpKernel {
 public:
  explicit COpKernel(OpKernelConstruction* ctx,
                     void* (*create_func)(TF_OpKernelConstruction*),
                     void (*compute_func)(void*, TF_OpKernelContext*),
                     void (*delete_func)(void*))
      : OpKernel(ctx), compute_func_(compute_func), delete_func_(delete_func) {
    if (create_func != nullptr) {
      c_kernel_ =
          (*create_func)(reinterpret_cast<TF_OpKernelConstruction*>(ctx));
    } else {
      c_kernel_ = nullptr;
    }
  }

  void Compute(OpKernelContext* ctx) override {
    (*compute_func_)(c_kernel_, reinterpret_cast<TF_OpKernelContext*>(ctx));
  }

  ~COpKernel() override {
    if (delete_func_ != nullptr) {
      (*delete_func_)(c_kernel_);
    }
  }

 private:
  void (*compute_func_)(void*, TF_OpKernelContext* context);
  void (*delete_func_)(void*);
  void* c_kernel_;
};

// A KernelFactory that returns COpKernel instances.
class KernelBuilderFactory
    : public ::tensorflow::kernel_factory::OpKernelFactory {
 public:
  explicit KernelBuilderFactory(TF_KernelBuilder* builder)
      : builder_(builder) {}
  ::tensorflow::OpKernel* Create(
      ::tensorflow::OpKernelConstruction* context) override {
    return new ::tensorflow::COpKernel(context, builder_->create_function,
                                       builder_->compute_function,
                                       builder_->delete_function);
  }
  ~KernelBuilderFactory() override { TF_DeleteKernelBuilder(builder_); }

 private:
  TF_KernelBuilder* builder_;
};
}  // namespace
}  // namespace tensorflow

void TF_RegisterKernelBuilder(const char* name, TF_KernelBuilder* builder,
                              TF_Status* status) {
  using tensorflow::register_kernel::Name;

  tensorflow::kernel_factory::OpKernelRegistrar(
      builder->cc_builder->Build(), name,
      absl::make_unique<tensorflow::KernelBuilderFactory>(builder));

  TF_SetStatus(status, TF_OK, "");
}

int TF_NumInputs(TF_OpKernelContext* ctx) {
  auto* cc_ctx = reinterpret_cast<::tensorflow::OpKernelContext*>(ctx);
  return cc_ctx->num_inputs();
}

int TF_NumOutputs(TF_OpKernelContext* ctx) {
  auto* cc_ctx = reinterpret_cast<::tensorflow::OpKernelContext*>(ctx);
  return cc_ctx->num_outputs();
}

void TF_GetInput(TF_OpKernelContext* ctx, int i, TF_Tensor** tensor,
                 TF_Status* status) {
  auto* cc_ctx = reinterpret_cast<::tensorflow::OpKernelContext*>(ctx);
  if (i < 0 || i >= cc_ctx->num_inputs()) {
    TF_SetStatus(status, TF_OUT_OF_RANGE, "input index out of range");
    return;
  }
  const ::tensorflow::Tensor& cc_tensor(cc_ctx->input(i));
  TF_Tensor* result =
      ::tensorflow::TF_TensorFromTensor(cc_tensor, &status->status);
  if (TF_GetCode(status) == TF_OK) {
    *tensor = result;
  }
}

void TF_SetOutput(TF_OpKernelContext* ctx, int i, const TF_Tensor* tensor,
                  TF_Status* status) {
  auto* cc_ctx = reinterpret_cast<::tensorflow::OpKernelContext*>(ctx);
  if (i < 0 || i >= cc_ctx->num_outputs()) {
    TF_SetStatus(status, TF_OUT_OF_RANGE, "output index out of range");
    return;
  }
  ::tensorflow::Tensor cc_tensor;
  ::tensorflow::Status s = ::tensorflow::TF_TensorToTensor(tensor, &cc_tensor);
  TF_SetStatus(status, TF_OK, "");
  ::tensorflow::Set_TF_Status_from_Status(status, s);
  if (s.ok()) {
    cc_ctx->set_output(i, cc_tensor);
  }
}

void TF_OpKernelConstruction_Failure(TF_OpKernelConstruction* ctx,
                                     TF_Status* status) {
  auto* cc_ctx = reinterpret_cast<::tensorflow::OpKernelConstruction*>(ctx);
  ::tensorflow::Status s(::tensorflow::StatusFromTF_Status(status));
  cc_ctx->CtxFailure(s);
}

void TF_OpKernelContext_Failure(TF_OpKernelContext* ctx, TF_Status* status) {
  auto* cc_ctx = reinterpret_cast<::tensorflow::OpKernelContext*>(ctx);
  ::tensorflow::Status s(::tensorflow::StatusFromTF_Status(status));
  cc_ctx->CtxFailure(s);
}

#define DEFINE_TF_GETATTR(func, c_type, cc_type)                               \
  void TF_OpKernelConstruction_GetAttr##func(TF_OpKernelConstruction* ctx,     \
                                             const char* attr_name,            \
                                             c_type* val, TF_Status* status) { \
    TF_SetStatus(status, TF_OK, "");                                           \
    cc_type v;                                                                 \
    auto* cc_ctx = reinterpret_cast<::tensorflow::OpKernelConstruction*>(ctx); \
    ::tensorflow::Status s = cc_ctx->GetAttr(attr_name, &v);                   \
    ::tensorflow::Set_TF_Status_from_Status(status, s);                        \
    if (s.ok()) {                                                              \
      *val = static_cast<c_type>(v);                                           \
    }                                                                          \
  }

DEFINE_TF_GETATTR(Type, TF_DataType, tensorflow::DataType)
DEFINE_TF_GETATTR(Int32, tensorflow::int32, int32_t)

TF_StringView TF_OpKernelConstruction_GetName(TF_OpKernelConstruction* ctx) {
  auto* cc_ctx = reinterpret_cast<tensorflow::OpKernelConstruction*>(ctx);
  TF_StringView string_view_of_name;
  string_view_of_name.data = cc_ctx->def().name().data();
  string_view_of_name.len = cc_ctx->def().name().length();
  return string_view_of_name;
}

TF_DataType TF_ExpectedOutputDataType(TF_OpKernelContext* ctx, int i) {
  auto* cc_ctx = reinterpret_cast<::tensorflow::OpKernelContext*>(ctx);
  return static_cast<TF_DataType>(cc_ctx->expected_output_dtype(i));
}

int64_t TF_StepId(TF_OpKernelContext* ctx) {
  return reinterpret_cast<::tensorflow::OpKernelContext*>(ctx)->step_id();
}

TF_Tensor* TF_AllocateOutput(TF_OpKernelContext* context, int index,
                             TF_DataType dtype, int64_t* dims, int num_dims,
                             size_t len, TF_Status* status) {
  TF_SetStatus(status, TF_OK, "");
  auto* cc_ctx = reinterpret_cast<::tensorflow::OpKernelContext*>(context);
  static_assert(sizeof(int64_t) == sizeof(tensorflow::int64),
                "64-bit int types should match in size");
  tensorflow::gtl::ArraySlice<tensorflow::int64> dimarray(
      reinterpret_cast<tensorflow::int64*>(dims), num_dims);
  tensorflow::Tensor* tensor;
  tensorflow::Status s = cc_ctx->allocate_output(
      index, tensorflow::TensorShape(dimarray), &tensor);
  if (!s.ok()) {
    ::tensorflow::Set_TF_Status_from_Status(status, s);
    return nullptr;
  }
  TF_Tensor* tf_tensor = TF_TensorFromTensor(*tensor, &s);
  if (!s.ok()) {
    ::tensorflow::Set_TF_Status_from_Status(status, s);
    return nullptr;
  }
  return tf_tensor;
}

TF_Tensor* TF_ForwardInputOrAllocateOutput(
    TF_OpKernelContext* context, int* candidate_input_indices,
    int num_candidate_input_indices, int output_index, int64_t* output_dims,
    int output_num_dims, int* forwarded_input, TF_Status* status) {
  TF_SetStatus(status, TF_OK, "");
  auto* cc_ctx = reinterpret_cast<::tensorflow::OpKernelContext*>(context);

  static_assert(sizeof(int64_t) == sizeof(tensorflow::int64),
                "64-bit int types should match in size");
  tensorflow::gtl::ArraySlice<int> input_indices_array(
      candidate_input_indices, num_candidate_input_indices);
  tensorflow::gtl::ArraySlice<tensorflow::int64> output_dimarray(
      reinterpret_cast<tensorflow::int64*>(output_dims), output_num_dims);
  tensorflow::Tensor* output_tensor_pointer;
  tensorflow::Status s = cc_ctx->forward_input_or_allocate_output(
      input_indices_array, output_index,
      tensorflow::TensorShape(output_dimarray), &output_tensor_pointer,
      forwarded_input);
  if (!s.ok()) {
    ::tensorflow::Set_TF_Status_from_Status(status, s);
    return nullptr;
  }
  TF_Tensor* tf_tensor_output = TF_TensorFromTensor(*output_tensor_pointer, &s);
  if (!s.ok()) {
    ::tensorflow::Set_TF_Status_from_Status(status, s);
    return nullptr;
  }
  return tf_tensor_output;
}

TF_Tensor* TF_AllocateTemp(TF_OpKernelContext* context, TF_DataType dtype,
                           int64_t* dims, int num_dims,
                           TF_AllocatorAttributes* attributes,
                           TF_Status* status) {
  auto* cc_ctx = reinterpret_cast<::tensorflow::OpKernelContext*>(context);
  TF_SetStatus(status, TF_OK, "");
  static_assert(sizeof(int64_t) == sizeof(tensorflow::int64),
                "64-bit int types should match in size");
  tensorflow::gtl::ArraySlice<tensorflow::int64> dimarray(
      reinterpret_cast<tensorflow::int64*>(dims), num_dims);
  if (attributes && !attributes->struct_size) {
    TF_SetStatus(
        status, TF_INVALID_ARGUMENT,
        "TF_AllocatorAttributes struct "
        "size member must be set to TF_ALLOCATOR_ATTRIBUTES_STRUCT_SIZE");
    return nullptr;
  }
  tensorflow::AllocatorAttributes allocator_attr;
  if (attributes && attributes->on_host) {
    allocator_attr.set_on_host(true);
  }
  tensorflow::Status s;
  tensorflow::Tensor tensor;
  s = cc_ctx->allocate_temp(static_cast<tensorflow::DataType>(dtype),
                            tensorflow::TensorShape(dimarray), &tensor,
                            allocator_attr);
  if (!s.ok()) {
    ::tensorflow::Set_TF_Status_from_Status(status, s);
    return nullptr;
  }
  TF_Tensor* tf_tensor;
  tf_tensor = TF_TensorFromTensor(tensor, &s);
  if (!s.ok()) {
    ::tensorflow::Set_TF_Status_from_Status(status, s);
    return nullptr;
  }
  return tf_tensor;
}
