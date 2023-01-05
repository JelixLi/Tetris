// RUN: tf-opt -tf-broadcast-fold %s | FileCheck %s

// CHECK-LABEL: @broadcast_mul0
func @broadcast_mul0(%arg0: tensor<5x7xf32>, %arg1: tensor<7xf32>) -> tensor<5x7xf32> {
  %cst = constant dense<[5, 7]> : tensor<2xi32>
  %0 = "tf.BroadcastTo"(%arg1, %cst) : (tensor<7xf32>, tensor<2xi32>) -> tensor<5x7xf32>
  %1 = "tf.Mul"(%arg0, %0) : (tensor<5x7xf32>, tensor<5x7xf32>) -> tensor<5x7xf32>
  return %1 : tensor<5x7xf32>
  // CHECK: %[[V0:.*]] = "tf.Mul"(%arg0, %arg1) : (tensor<5x7xf32>, tensor<7xf32>) -> tensor<5x7xf32>
  // CHECK: %[[V0]] : tensor<5x7xf32>
}

// CHECK-LABEL: @broadcast_mul1
func @broadcast_mul1(%arg0: tensor<7xf32>, %arg1: tensor<5x7xf32>) -> tensor<5x7xf32> {
  %cst = constant dense<[5, 7]> : tensor<2xi32>
  %0 = "tf.BroadcastTo"(%arg0, %cst) : (tensor<7xf32>, tensor<2xi32>) -> tensor<5x7xf32>
  %1 = "tf.Mul"(%0, %arg1) : (tensor<5x7xf32>, tensor<5x7xf32>) -> tensor<5x7xf32>
  return %1 : tensor<5x7xf32>
  // CHECK: %[[V0:.*]] = "tf.Mul"(%arg0, %arg1) : (tensor<7xf32>, tensor<5x7xf32>) -> tensor<5x7xf32>
  // CHECK: %[[V0]] : tensor<5x7xf32>
}

// CHECK-LABEL: @broadcast_add_implicit_fold
func @broadcast_add_implicit_fold(%arg0: tensor<5x1xf32>, %arg1: tensor<7xf32>) -> tensor<5x7xf32> {
  %cst = constant dense<[5, 7]> : tensor<2xi32>
  %0 = "tf.BroadcastTo"(%arg1, %cst) : (tensor<7xf32>, tensor<2xi32>) -> tensor<5x7xf32>
  %1 = "tf.AddV2"(%arg0, %0) : (tensor<5x1xf32>, tensor<5x7xf32>) -> tensor<5x7xf32>
  return %1 : tensor<5x7xf32>
  // CHECK: %[[V0:.*]] = "tf.AddV2"(%arg0, %arg1) : (tensor<5x1xf32>, tensor<7xf32>) -> tensor<5x7xf32>
  // CHECK: %[[V0]] : tensor<5x7xf32>
}

// CHECK-LABEL: @broadcast_mul_implicit_no_fold
func @broadcast_mul_implicit_no_fold(%arg0: tensor<5x7xf32>, %arg1: tensor<5xf32>) -> tensor<3x5x7xf32> {
  %cst = constant dense<[3, 5, 7]> : tensor<3xi32>
  %0 = "tf.BroadcastTo"(%arg1, %cst) : (tensor<5xf32>, tensor<3xi32>) -> tensor<3x5x7xf32>
  %1 = "tf.Mul"(%arg0, %0) : (tensor<5x7xf32>, tensor<3x5x7xf32>) -> tensor<3x5x7xf32>
  return %1 : tensor<3x5x7xf32>
  // CHECK: %[[C0:.*]] = constant dense<[3, 5, 7]> : tensor<3xi32>
  // CHECK: %[[V0:.*]] = "tf.BroadcastTo"(%arg1, %[[C0]]) : (tensor<5xf32>, tensor<3xi32>) -> tensor<3x5x7xf32>
  // CHECK: %[[V1:.*]] = "tf.Mul"(%arg0, %[[V0]]) : (tensor<5x7xf32>, tensor<3x5x7xf32>) -> tensor<3x5x7xf32>
  // CHECK: %[[V1]] : tensor<3x5x7xf32>
}
