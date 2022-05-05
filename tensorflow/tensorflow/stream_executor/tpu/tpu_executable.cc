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

#include "tensorflow/stream_executor/tpu/tpu_executable.h"

#include "tensorflow/compiler/xla/xla_data.pb.h"
#include "tensorflow/core/platform/casts.h"
#include "tensorflow/core/tpu/kernels/tpu_execute_c_api.h"
#include "tensorflow/core/tpu/tpu_api.h"
#include "tensorflow/stream_executor/tpu/c_api_conversions.h"
#include "tensorflow/stream_executor/tpu/proto_helper.h"
#include "tensorflow/stream_executor/tpu/status_helper.h"
#include "tensorflow/stream_executor/tpu/tpu_platform.h"
#include "tensorflow/stream_executor/tpu/tpu_platform_interface.h"

namespace xla {

TpuExecutable::TpuExecutable(const XLA_TpuProgram* core_program,
                             std::unique_ptr<HloModule> hlo_module,
                             HostCommandHandler host_command_handler)
    : TpuExecutableInterface(std::move(hlo_module),
                             /*hlo_profile_printer_data=*/nullptr,
                             /*hlo_profile_index_map=*/nullptr),
      core_program_(core_program),
      host_command_handler_(std::move(host_command_handler)) {}

Status TpuExecutable::LoadProgramAndEnqueueToStream(
    const ServiceExecutableRunOptions& run_options,
    absl::Span<const se::DeviceMemoryBase> arguments,
    se::DeviceMemoryBase result,
    absl::optional<se::DeviceMemoryBase> cross_program_prefetch_addr) {
  SE_DeviceMemoryBase* arguments_bases = nullptr;
  if (!arguments.empty()) {
    arguments_bases = new SE_DeviceMemoryBase[arguments.size()];
    for (int i = 0; i < arguments.size(); i++) {
      arguments_bases[i] =
          SE_DeviceMemoryBase{const_cast<void*>(arguments[i].opaque()),
                              arguments[i].size(), arguments[i].payload()};
    }
  }

  SE_DeviceMemoryBase result_base{result.opaque(), result.size(),
                                  result.payload()};
  SE_DeviceMemoryBase prefetch_base;
  if (cross_program_prefetch_addr.has_value()) {
    prefetch_base = SE_DeviceMemoryBase{cross_program_prefetch_addr->opaque(),
                                        cross_program_prefetch_addr->size(),
                                        cross_program_prefetch_addr->payload()};
  }
  int32 rng_seed = run_options.run_options().rng_seed();

  XLA_DeviceAssignment c_dev_assign{/*bytes=*/nullptr, /*size=*/0};
  auto dev_assign = run_options.run_options().device_assignment();
  stream_executor::tpu::SerializedProto dev_assign_serialized;
  if (dev_assign != nullptr) {
    DeviceAssignmentProto dev_assign_proto;
    TF_RETURN_IF_ERROR(dev_assign->Serialize(&dev_assign_proto));
    dev_assign_serialized =
        stream_executor::tpu::SerializeProto(dev_assign_proto);
    c_dev_assign.bytes = dev_assign_serialized.bytes;
    c_dev_assign.size = dev_assign_serialized.size;
  }

  auto platform = tensorflow::down_cast<tensorflow::tpu::TpuPlatform*>(
      tensorflow::tpu::TpuPlatformInterface::GetRegisteredPlatform());
  auto stream = platform->stream_map()->at(
      run_options.run_options().stream()->implementation());
  StatusHelper status;

  tensorflow::tpu::ExecuteApiFn()
      ->TpuExecutable_LoadProgramAndEnqueueToStreamFn(
          core_program_, arguments_bases, arguments.size(), &result_base,
          (cross_program_prefetch_addr.has_value() ? &prefetch_base : nullptr),
          rng_seed, &c_dev_assign, stream, status.c_status);

  if (dev_assign != nullptr) {
    stream_executor::tpu::SerializedProto_Free(dev_assign_serialized);
  }
  delete[] arguments_bases;
  return status.status();
}

Shape TpuExecutable::HostShapeToDeviceShape(const Shape& host_shape) {
  XLA_Shape c_host_shape;
  XLA_Shape c_device_shape;
  ApiConverter::ToC(host_shape, &c_host_shape);
  tensorflow::tpu::ExecuteApiFn()->HardwareLayout_HostShapeToDeviceShapeFn(
      &c_host_shape, &c_device_shape);
  Shape device_shape = ApiConverter::FromC(&c_device_shape);
  ApiConverter::Free(&c_host_shape);
  ApiConverter::Free(&c_device_shape);
  return device_shape;
}

int64 TpuExecutable::ShapeSize(const Shape& shape) {
  XLA_Shape c_shape;
  ApiConverter::ToC(shape, &c_shape);
  int64 size =
      tensorflow::tpu::ExecuteApiFn()->HardwareLayout_ShapeSizeFn(&c_shape);
  ApiConverter::Free(&c_shape);
  return size;
}

absl::string_view TpuExecutable::fingerprint() const {
  // TODO(skye): the fingerprint can be plumbed through via core_program_
  LOG(FATAL) << "TpuExecutable::fingerprint() unimplemented";
}

}  // namespace xla
