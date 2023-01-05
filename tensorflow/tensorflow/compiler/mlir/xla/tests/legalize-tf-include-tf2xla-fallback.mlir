// RUN: tf-opt "-xla-legalize-tf=allow-partial-conversion use-tf2xla-fallback=false" -verify-diagnostics %s | FileCheck --check-prefix NO_FALLBACK %s
// RUN: tf-opt "-xla-legalize-tf=use-tf2xla-fallback=true device-type=XLA_CPU_JIT" -verify-diagnostics %s | FileCheck --check-prefix SUPPORTED_FALLBACK_DEVICE %s
// RUN: tf-opt "-xla-legalize-tf=allow-partial-conversion use-tf2xla-fallback=true" %s | FileCheck --check-prefix UNSPECIFIED_FALLBACK_DEVICE %s
// RUN: tf-opt "-xla-legalize-tf=allow-partial-conversion use-tf2xla-fallback=true device-type=INVALID_DEVICE_TYPE" %s | FileCheck --check-prefix UNSUPPORTED_FALLBACK_DEVICE %s

// We run this test four times:
// 1) Legalize without using TF2XLA fallback (ops cannot be legalized).
// 2) Use fallback with a device that supports all ops (ops can be legalized).
// 3) Use fallback with unspecified device (ops cannot be legalized).
// 4) Use fallback with specified but unsupported device (ops cannot be legalized).
//
// Note: For 3) and 4) we do not use `-verify-diagnostics` because these cases
// produce remarks that don't occur for 1) and 2) and there is no way to check
// the remarks only for 3) and 4) (except using two files).

module attributes {tf.versions = {bad_consumers = [], min_consumer = 0 : i32, producer = 268 : i32}} {

// CHECK-LABEL: non_max_suppression_v4
func @non_max_suppression_v4(%arg0: tensor<3x4xf32>, %arg1: tensor<3xf32>, %arg2: tensor<f32>, %arg3: tensor<f32>) -> tensor<2xi32> {
  %max_size = mhlo.constant dense<2> : tensor<i32>
  // NO_FALLBACK: tf.NonMaxSuppressionV4
  // SUPPORTED_FALLBACK_DEVICE-NOT: tf.NonMaxSuppressionV4
  // UNSPECIFIED_FALLBACK_DEVICE: tf.NonMaxSuppressionV4
  // UNSUPPORTED_FALLBACK_DEVICE:  tf.NonMaxSuppressionV4
  %0:2 = "tf.NonMaxSuppressionV4"(%arg0, %arg1, %max_size, %arg2, %arg3) {pad_to_max_output_size = true}: (tensor<3x4xf32>, tensor<3xf32>, tensor<i32>, tensor<f32>, tensor<f32>) -> (tensor<2xi32>, tensor<i32>)
  return %0#0 : tensor<2xi32>
}

// CHECK-LABEL: mirror_pad
func @mirror_pad(%arg0: tensor<2x3xcomplex<f64>>) -> tensor<4x7xcomplex<f64>> {
  %0 = mhlo.constant dense<[[1, 1], [2, 2]]> : tensor<2x2xi32>
  // NO_FALLBACK: tf.MirrorPad
  // SUPPORTED_FALLBACK_DEVICE-NOT: tf.MirrorPad
  // UNSPECIFIED_FALLBACK_DEVICE: tf.MirrorPad
  // UNSUPPORTED_FALLBACK_DEVICE: tf.MirrorPad
  %1 = "tf.MirrorPad"(%arg0, %0) {mode = "SYMMETRIC"} : (tensor<2x3xcomplex<f64>>, tensor<2x2xi32>) -> tensor<4x7xcomplex<f64>>
  return %1 : tensor<4x7xcomplex<f64>>
}

// CHECK-LABEL: atan2
func @atan2(%arg0: tensor<4x1xf32>, %arg1: tensor<4x1x4xf32>) -> tensor<4x4x4xf32> {
  // NO_FALLBACK: tf.Atan2
  // SUPPORTED_FALLBACK_DEVICE-NOT: tf.Atan2
  // UNSPECIFIED_FALLBACK_DEVICE: tf.Atan2
  // UNSUPPORTED_FALLBACK_DEVICE: tf.Atan2
  %0 = "tf.Atan2"(%arg0, %arg1) : (tensor<4x1xf32>, tensor<4x1x4xf32>) -> tensor<4x4x4xf32>
  return %0: tensor<4x4x4xf32>
}

}