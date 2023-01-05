// RUN: tf-opt %s --xla-legalize-tf | mlir-hlo-opt --transform-unranked-hlo | kernel-gen-opt -allow-unregistered-dialect --shape-to-descriptors --canonicalize --bufferize | FileCheck %s

// Test whether all shape computations required for tanh can be lowered to
// the standard dialect, scf and descriptors. We check for a sparse pattern here,
// as each lowering pattern is already tested and we just care for the
// integration.
// TODO: Expand this pattern once things have stabilized.
// CHECK-LABEL: @tanh
func @tanh(%arg0: tensor<*xf32>) -> tensor<*xf32> {
  // CHECK: alloca
  // CHECK: scf.parallel
  // CHECK-NOT: tensor_load
  // CHECK: scf.for
  // CHECK-NOT: tensor_from_elements
  // CHECK: mhlo.reshape_memref_cast
  // CHECK: lmhlo.tanh
  // CHECK: mhlo.reshape_memref_cast
  %0 = "tf.Tanh"(%arg0) { } : (tensor<*xf32>) -> tensor<*xf32>
  return %0 : tensor<*xf32>
}
