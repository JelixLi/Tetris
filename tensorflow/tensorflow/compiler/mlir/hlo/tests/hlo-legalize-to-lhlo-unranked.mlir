// RUN: mlir-hlo-opt -hlo-legalize-to-lhlo=results-escape-function=true -buffer-placement %s -o - | FileCheck %s

// CHECK-LABEL: func @func_op_unranked_arg_result
func @func_op_unranked_arg_result(%arg0: tensor<*xf32>) -> tensor<*xf32> {
  return %arg0 : tensor<*xf32>
}
// CHECK-SAME: ([[ARG:%.*]]: memref<*xf32>) -> memref<*xf32>
// CHECK-NEXT: return [[ARG]] : memref<*xf32>

// -----

// CHECK-LABEL: func @dynamic_reshape_from_unranked
func @dynamic_reshape_from_unranked(
         %operand: tensor<*xf32>, %shape: tensor<1xi32>) -> tensor<?xf32> {
  %reshaped = "mhlo.dynamic_reshape"(%operand, %shape)
      : (tensor<*xf32>, tensor<1xi32>) -> tensor<?xf32>
  return %reshaped : tensor<?xf32>
}
// CHECK-SAME: ([[ARG:%.*]]: memref<*xf32>, [[SHAPE:%.*]]: memref<1xi32>)
// CHECK-NEXT: reshape_memref_cast [[ARG]]([[SHAPE]])
// CHECK-SAME:   : (memref<*xf32>, memref<1xi32>) -> memref<?xf32>

// -----

// CHECK-LABEL: func @dynamic_reshape_to_unranked
func @dynamic_reshape_to_unranked(
         %operand: tensor<?xf32>, %shape: tensor<?xi32>) -> tensor<*xf32> {
  %reshaped = "mhlo.dynamic_reshape"(%operand, %shape)
      : (tensor<?xf32>, tensor<?xi32>) -> tensor<*xf32>
  return %reshaped : tensor<*xf32>
}
// CHECK-SAME: ([[ARG:%.*]]: memref<?xf32>, [[SHAPE:%.*]]: memref<?xi32>)
// CHECK-NEXT: reshape_memref_cast [[ARG]]([[SHAPE]])
// CHECK-SAME:   : (memref<?xf32>, memref<?xi32>) -> memref<*xf32>
