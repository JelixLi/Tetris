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

#include "tensorflow/lite/delegates/gpu/cl/selectors/simple_selectors.h"

#include <memory>
#include <set>

#include "absl/memory/memory.h"
#include "tensorflow/lite/delegates/gpu/cl/kernels/add.h"
#include "tensorflow/lite/delegates/gpu/cl/kernels/concat_xy.h"
#include "tensorflow/lite/delegates/gpu/cl/kernels/concat_z.h"
#include "tensorflow/lite/delegates/gpu/cl/kernels/depthwise_conv.h"
#include "tensorflow/lite/delegates/gpu/cl/kernels/lstm.h"
#include "tensorflow/lite/delegates/gpu/cl/kernels/max_unpooling.h"
#include "tensorflow/lite/delegates/gpu/cl/kernels/mean.h"
#include "tensorflow/lite/delegates/gpu/cl/kernels/padding.h"
#include "tensorflow/lite/delegates/gpu/cl/kernels/pooling.h"
#include "tensorflow/lite/delegates/gpu/cl/kernels/prelu.h"
#include "tensorflow/lite/delegates/gpu/cl/kernels/quantize_and_dequantize.h"
#include "tensorflow/lite/delegates/gpu/cl/kernels/relu.h"
#include "tensorflow/lite/delegates/gpu/cl/kernels/reshape.h"
#include "tensorflow/lite/delegates/gpu/cl/kernels/reshapex4.h"
#include "tensorflow/lite/delegates/gpu/cl/kernels/resize.h"
#include "tensorflow/lite/delegates/gpu/cl/kernels/softmax.h"
#include "tensorflow/lite/delegates/gpu/cl/kernels/softmax1x1.h"
#include "tensorflow/lite/delegates/gpu/cl/kernels/space_to_depth.h"
#include "tensorflow/lite/delegates/gpu/cl/kernels/strided_slice.h"
#include "tensorflow/lite/delegates/gpu/cl/kernels/transpose.h"
#include "tensorflow/lite/delegates/gpu/cl/kernels/winograd.h"
#include "tensorflow/lite/delegates/gpu/common/status.h"

namespace tflite {
namespace gpu {
namespace cl {

std::unique_ptr<GPUOperation> SelectLSTM(const OperationDef& op_def,
                                         const DeviceInfo& device_info) {
  return absl::make_unique<GPUOperation>(CreateLSTM(op_def, device_info));
}

std::unique_ptr<GPUOperation> SelectReLU(const ReLUAttributes& attr,
                                         const OperationDef& op_def) {
  return absl::make_unique<GPUOperation>(CreateReLU(op_def, attr));
}

std::unique_ptr<GPUOperation> SelectPReLU(const PReLUAttributes& attr,
                                          const DeviceInfo& device_info,
                                          const OperationDef& op_def) {
  return absl::make_unique<GPUOperation>(
      CreatePReLU(device_info, op_def, attr));
}

std::unique_ptr<GPUOperation> SelectPooling(const Pooling2DAttributes& attr,
                                            const OperationDef& op_def) {
  return absl::make_unique<GPUOperation>(CreatePooling(op_def, attr));
}

std::unique_ptr<GPUOperation> SelectMaxUnpooling(
    const MaxUnpooling2DAttributes& attr, const OperationDef& op_def) {
  return absl::make_unique<GPUOperation>(CreateMaxUnpooling(op_def, attr));
}

void SelectAdd(const OperationDef& op_def, const std::vector<int>& channels,
               int dst_channels, std::unique_ptr<GPUOperation>* ptr) {
  GPUOperation operation = CreateAdd(op_def, channels, dst_channels);
  *ptr = absl::make_unique<GPUOperation>(std::move(operation));
}

absl::Status SelectResize(const Resize2DAttributes& attr,
                          const OperationDef& op_def,
                          std::unique_ptr<GPUOperation>* ptr) {
  Resize operation = CreateResize(op_def, attr);
  *ptr = absl::make_unique<Resize>(std::move(operation));
  return absl::OkStatus();
}

absl::Status SelectConcat(const ConcatAttributes& attr,
                          const std::vector<int>& channels,
                          const OperationDef& op_def,
                          const DeviceInfo& device_info,
                          std::unique_ptr<GPUOperation>* ptr) {
  switch (attr.axis) {
    case Axis::CHANNELS: {
      GPUOperation operation = CreateConcatZ(op_def, channels, device_info);
      *ptr = absl::make_unique<GPUOperation>(std::move(operation));
      return absl::OkStatus();
    }
    case Axis::BATCH:
    case Axis::DEPTH:
    case Axis::HEIGHT:
    case Axis::WIDTH: {
      GPUOperation operation = CreateConcatXY(op_def, attr);
      *ptr = absl::make_unique<GPUOperation>(std::move(operation));
      return absl::OkStatus();
    }
    default:
      return absl::UnimplementedError("No concat for this axis.");
  }
}

std::unique_ptr<GPUOperation> SelectDWConvolutionDynamicWeights(
    const DepthwiseConvolution2DAttributes& attr, const DeviceInfo& device_info,
    const OperationDef& op_def) {
  return absl::make_unique<GPUOperation>(
      CreateDepthwiseConvolution2DDynamicWeights(device_info, op_def, attr));
}

void SelectReshape(int src_channels, int dst_channels,
                   const OperationDef& op_def,
                   std::unique_ptr<GPUOperation>* ptr) {
  if (src_channels % 4 == 0 && dst_channels % 4 == 0) {
    GPUOperation operation = CreateReshapex4(op_def);
    *ptr = absl::make_unique<GPUOperation>(std::move(operation));
  } else {
    GPUOperation operation = CreateReshape(op_def);
    *ptr = absl::make_unique<GPUOperation>(std::move(operation));
  }
}

void SelectSpaceToDepth(const SpaceToDepthAttributes& attr,
                        const OperationDef& op_def,
                        std::unique_ptr<GPUOperation>* ptr) {
  GPUOperation operation = CreateSpaceToDepth(op_def, attr);
  *ptr = absl::make_unique<GPUOperation>(std::move(operation));
}

void SelectPadding(const PadAttributes& attr, const OperationDef& op_def,
                   std::unique_ptr<GPUOperation>* ptr) {
  GPUOperation operation = CreatePadding(op_def, attr);
  *ptr = absl::make_unique<GPUOperation>(std::move(operation));
}

void SelectStridedSlice(const SliceAttributes& attr, const OperationDef& op_def,
                        std::unique_ptr<GPUOperation>* ptr) {
  StridedSlice operation = CreateStridedSlice(op_def, attr);
  *ptr = absl::make_unique<StridedSlice>(std::move(operation));
}

absl::Status SelectMean(const MeanAttributes& attr, const OperationDef& op_def,
                        const DeviceInfo& device_info,
                        std::unique_ptr<GPUOperation>* ptr) {
  if (attr.dims != std::set<Axis>({Axis::HEIGHT, Axis::WIDTH})) {
    return absl::UnimplementedError("Mean operation supports only HW plane");
  }
  Mean operation = CreateMean(op_def, device_info);
  *ptr = absl::make_unique<Mean>(std::move(operation));
  return absl::OkStatus();
}

void SelectSoftmax(const BHWC& shape, const OperationDef& op_def,
                   std::unique_ptr<GPUOperation>* ptr) {
  if (shape.w == 1 && shape.h == 1) {
    Softmax1x1 operation = CreateSoftmax1x1(op_def);
    *ptr = absl::make_unique<Softmax1x1>(std::move(operation));
  } else {
    GPUOperation operation = CreateSoftmax(op_def);
    *ptr = absl::make_unique<GPUOperation>(std::move(operation));
  }
}

void SelectTranspose(const TransposeAttributes& attr,
                     const OperationDef& op_def,
                     std::unique_ptr<GPUOperation>* ptr) {
  GPUOperation operation = CreateTranspose(op_def, attr);
  *ptr = absl::make_unique<GPUOperation>(std::move(operation));
}

std::unique_ptr<GPUOperation> SelectWinograd4x4To36(
    const DeviceInfo& device_info, const Padding2D& padding,
    const OperationDef& op_def) {
  return absl::make_unique<Winograd4x4To36>(
      CreateWinograd4x4To36(device_info, op_def, padding));
}

std::unique_ptr<GPUOperation> SelectWinograd36To4x4(
    const DeviceInfo& device_info, const OperationDef& op_def,
    const tflite::gpu::Tensor<Linear, DataType::FLOAT32>& biases) {
  return absl::make_unique<Winograd36To4x4>(
      CreateWinograd36To4x4(device_info, op_def, biases));
}

std::unique_ptr<GPUOperation> SelectQuantizeAndDequantize(
    const QuantizeAndDequantizeAttributes& attr, const OperationDef& op_def) {
  return absl::make_unique<GPUOperation>(
      CreateQuantizeAndDequantize(op_def, attr));
}

}  // namespace cl
}  // namespace gpu
}  // namespace tflite
