// RUN: tf-opt %s -pass-pipeline='func(canonicalize)' | FileCheck %s

// CHECK-LABEL: func @tfAssertTrue
func @tfAssertTrue(%arg0: tensor<1x1x6x2xf32>) {
  %t = constant dense<true> : tensor<i1>
  // CHECK-NOT: tf.Assert
  "tf.Assert"(%t, %arg0) {summarize = 3} : (tensor<i1>, tensor<1x1x6x2xf32>) -> ()
  return
}

// CHECK-LABEL: func @tfAssertFalse
func @tfAssertFalse(%arg0: tensor<1x1x6x2xf32>) {
  %f = constant dense<false> : tensor<i1>
  // CHECK: tf.Assert
  "tf.Assert"(%f, %arg0) {summarize = 3} : (tensor<i1>, tensor<1x1x6x2xf32>) -> ()
  return
}

// CHECK-LABEL: testBatchMatMulToMatMul
func @testBatchMatMulToMatMul(%arg0: tensor<2x3xf32>, %arg1: tensor<3x2xf32>) -> tensor<2x2xf32> {
  %0 = "tf.BatchMatMul"(%arg0, %arg1) {adj_x = false, adj_y = false} : (tensor<2x3xf32>, tensor<3x2xf32>) -> tensor<2x2xf32>
  return %0: tensor<2x2xf32>

// CHECK: %0 = "tf.MatMul"(%arg0, %arg1) {transpose_a = false, transpose_b = false} : (tensor<2x3xf32>, tensor<3x2xf32>) -> tensor<2x2xf32>
// CHECK: return %0
}

// CHECK-LABEL: testBatchMatMulV2ToMatMul
func @testBatchMatMulV2ToMatMul(%arg0: tensor<4x3xf32>, %arg1: tensor<4x5xf32>) -> tensor<3x5xf32> {
  %0 = "tf.BatchMatMulV2"(%arg0, %arg1) {adj_x = true, adj_y = false} : (tensor<4x3xf32>, tensor<4x5xf32>) -> tensor<3x5xf32>
  return %0: tensor<3x5xf32>

// CHECK: %0 = "tf.MatMul"(%arg0, %arg1) {transpose_a = true, transpose_b = false} : (tensor<4x3xf32>, tensor<4x5xf32>) -> tensor<3x5xf32>
// CHECK: return %0
}

// CHECK-LABEL: testBiasAddV1ToBiasAdd
func @testBiasAddV1ToBiasAdd(%arg0: tensor<*xf32>, %arg1: tensor<128xf32>) -> tensor<*xf32> {
  // CHECK: "tf.BiasAdd"(%arg0, %arg1) {data_format = "NHWC"} : (tensor<*xf32>, tensor<128xf32>) -> tensor<*xf32>
  %0 = "tf.BiasAddV1"(%arg0, %arg1) : (tensor<*xf32>, tensor<128xf32>) -> tensor<*xf32>
  return %0: tensor<*xf32>
}

// CHECK-LABEL: func @testLeakyRelu
func @testLeakyRelu(%arg0 : tensor<16xf32>) -> (tensor<16xf32>) {
  %2 = "tf.LeakyRelu"(%arg0) {alpha = 1.0 : f32} : (tensor<16xf32>) -> tensor<16xf32>
  // CHECK: return %arg0
  return %2 : tensor<16xf32>
}

// CHECK-LABEL: testSameBitcastType
func @testSameBitcastType(%arg0: tensor<8x16x32x64xf32>) -> tensor<8x16x32x64xf32> {
  %0 = "tf.Bitcast"(%arg0) : (tensor<8x16x32x64xf32>) -> tensor<8x16x32x64xf32>
  return %0: tensor<8x16x32x64xf32>

// CHECK: return %arg0
}

// CHECK-LABEL: testDifferentBitcastType
func @testDifferentBitcastType(%arg0: tensor<8x16x32x64xf32>) -> tensor<8x16x32x64xi32> {
  %0 = "tf.Bitcast"(%arg0) : (tensor<8x16x32x64xf32>) -> tensor<8x16x32x64xi32>
  return %0: tensor<8x16x32x64xi32>

// CHECK: %0 = "tf.Bitcast"(%arg0) : (tensor<8x16x32x64xf32>) -> tensor<8x16x32x64xi32>
// CHECK: return %0
}

// CHECK-LABEL: testDoubleBitcast
func @testDoubleBitcast(%arg0: tensor<8x16x32x64xf32>) -> tensor<8x16x32x64xi32> {
  %0 = "tf.Bitcast"(%arg0) : (tensor<8x16x32x64xf32>) -> tensor<8x16x32x64x2xi16>
  %1 = "tf.Bitcast"(%0) : (tensor<8x16x32x64x2xi16>) -> tensor<8x16x32x64xi32>
  return %1: tensor<8x16x32x64xi32>

// CHECK: %0 = "tf.Bitcast"(%arg0) : (tensor<8x16x32x64xf32>) -> tensor<8x16x32x64xi32>
// CHECK: return %0
}

// CHECK-LABEL: testDoubleBitcastWithDependentArg
func @testDoubleBitcastWithDependentArg(%arg0: tensor<8x16x32x64xf32>) -> (tensor<8x16x32x64xi32>, tensor<8x16x32x64x2xi16>) {
  %0 = "tf.Bitcast"(%arg0) : (tensor<8x16x32x64xf32>) -> tensor<8x16x32x64x2xi16>
  %1 = "tf.Bitcast"(%0) : (tensor<8x16x32x64x2xi16>) -> tensor<8x16x32x64xi32>
  %2 = "tf.Identity"(%0) : (tensor<8x16x32x64x2xi16>) -> tensor<8x16x32x64x2xi16>
  return %1, %2 :  tensor<8x16x32x64xi32>, tensor<8x16x32x64x2xi16>

// CHECK: %0 = "tf.Bitcast"(%arg0) : (tensor<8x16x32x64xf32>) -> tensor<8x16x32x64x2xi16>
// CHECK: %1 = "tf.Bitcast"(%arg0) : (tensor<8x16x32x64xf32>) -> tensor<8x16x32x64xi32>
// CHECK: %2 = "tf.Identity"(%0) : (tensor<8x16x32x64x2xi16>) -> tensor<8x16x32x64x2xi16>
// CHECK: return %1, %2
}

// CHECK-LABEL: testSameCastType
func @testSameCastType(%arg0: tensor<8x16x32x64xf32>) -> (tensor<8x16x32x64xf32>, tensor<8x16x32x64xf32>) {
  %0 = "tf.Cast"(%arg0) {Truncate = false} : (tensor<8x16x32x64xf32>) -> tensor<8x16x32x64xf32>
  %1 = "tf.Cast"(%arg0) {Truncate = true} : (tensor<8x16x32x64xf32>) -> tensor<8x16x32x64xf32>
  return %0, %1: tensor<8x16x32x64xf32>, tensor<8x16x32x64xf32>

// CHECK: return %arg0, %arg0
}

// CHECK-LABEL: testDifferentCastType
func @testDifferentCastType(%arg0: tensor<8x16x32x64xf32>) -> (tensor<8x16x32x64xi32>, tensor<8x16x32x64xi32>) {
  %0 = "tf.Cast"(%arg0) {Truncate = false} : (tensor<8x16x32x64xf32>) -> tensor<8x16x32x64xi32>
  %1 = "tf.Cast"(%arg0) {Truncate = true} : (tensor<8x16x32x64xf32>) -> tensor<8x16x32x64xi32>
  return %0, %1: tensor<8x16x32x64xi32>, tensor<8x16x32x64xi32>

// CHECK: %0 = "tf.Cast"(%arg0) {Truncate = false} : (tensor<8x16x32x64xf32>) -> tensor<8x16x32x64xi32>
// CHECK: %1 = "tf.Cast"(%arg0) {Truncate = true} : (tensor<8x16x32x64xf32>) -> tensor<8x16x32x64xi32>
// CHECK: return %0, %1
}

// CHECK-LABEL: testCompatibleCastType
func @testCompatibleCastType(%arg0: tensor<?xf32>) -> (tensor<10xf32>, tensor<10xf32>) {
  %0 = "tf.Cast"(%arg0) {Truncate = false} : (tensor<?xf32>) -> tensor<10xf32>
  %1 = "tf.Cast"(%arg0) {Truncate = true} : (tensor<?xf32>) -> tensor<10xf32>
  return %0, %1: tensor<10xf32>, tensor<10xf32>

// CHECK: %0 = "tf.Cast"(%arg0) {Truncate = false} : (tensor<?xf32>) -> tensor<10xf32>
// CHECK: %1 = "tf.Cast"(%arg0) {Truncate = true} : (tensor<?xf32>) -> tensor<10xf32>
// CHECK: return %0, %1
}

// CHECK-LABEL: testSameCastTypeAcrossBasicBlocks
func @testSameCastTypeAcrossBasicBlocks(tensor<8x16x32x64xf32>) -> tensor<8x16x32x64xf32> {
^bb0(%arg0: tensor<8x16x32x64xf32>):
  %0 = "tf.Cast"(%arg0) {Truncate = false} : (tensor<8x16x32x64xf32>) -> tensor<8x16x32x64xf32>
  br ^bb1
^bb1:
  %1 = "tf.Cast"(%0) {Truncate = true} : (tensor<8x16x32x64xf32>) -> tensor<8x16x32x64xf32>
  br ^exit
^exit:
  return %1: tensor<8x16x32x64xf32>

// CHECK: return %arg0
}

// CHECK-LABEL: testConcatCanonicalization
func @testConcatCanonicalization(%arg0: tensor<2x1xi32>, %arg1: tensor<2x1xi32>) -> tensor<2x2xi32> {
  // CHECK: %[[AXIS:.*]] = "tf.Const"
  %0 = "tf.Const"() { value = dense<1> : tensor<i32> } : () -> tensor<i32>

  // CHECK: "tf.ConcatV2"(%arg0, %arg1, %[[AXIS]])
  %1 = "tf.Concat"(%0, %arg0, %arg1) : (tensor<i32>, tensor<2x1xi32>, tensor<2x1xi32>) -> tensor<2x2xi32>
  return %1 : tensor<2x2xi32>
}

// CHECK-LABEL: testConcatCwiseUnary
func @testConcatCwiseUnary(%arg0: tensor<?x1xf32>, %arg1: tensor<?x1xf32>, %arg2: tensor<i32>) -> tensor<?x2xf32> {

  // CHECK: %[[CONCAT:.*]] = "tf.ConcatV2"(%arg0, %arg1, %arg2)
  // CHECK: %[[LOG1P:.*]] = "tf.Log1p"(%[[CONCAT]])
  // CHECK: return %[[LOG1P]]
  %0 = "tf.Log1p"(%arg0) : (tensor<?x1xf32>) -> tensor<?x1xf32>
  %1 = "tf.Log1p"(%arg1) : (tensor<?x1xf32>) -> tensor<?x1xf32>
  %2 = "tf.ConcatV2"(%0, %1, %arg2) : (tensor<?x1xf32>, tensor<?x1xf32>, tensor<i32>) -> tensor<?x2xf32>

  return %2 : tensor<?x2xf32>
}

// CHECK-LABEL: testConcatCwiseBinaryOnInnerDim
func @testConcatCwiseBinaryOnInnerDim(%arg0: tensor<?x1xf32>,
  %arg1: tensor<?x1xf32>, %arg2: tensor<f32>, %arg3: tensor<f32>) -> tensor<?x2xf32> {

  // CHECK: %[[LHS_AXIS:.*]] = "tf.Const"() {value = dense<1> : tensor<i64>}
  // CHECK: %[[RHS_AXIS:.*]] = "tf.Const"() {value = dense<0> : tensor<i64>}

  // CHECK: %[[LHS_CONCAT:.*]] = "tf.ConcatV2"(%arg0, %arg1, %[[LHS_AXIS]])
  // CHECK: %[[RHS_CONCAT:.*]] = "tf.ConcatV2"(%arg2, %arg3, %[[RHS_AXIS]])

  // CHECK: %[[MUL:.*]] = "tf.Mul"(%[[LHS_CONCAT]], %[[RHS_CONCAT]])
  // CHECK-SAME: (tensor<?x2xf32>, tensor<2xf32>) -> tensor<?x2xf32>
  // CHECK: return %[[MUL]]

  %0 = "tf.Const"() { value = dense<1> : tensor<i32> } : () -> tensor<i32>
  %1 = "tf.Mul"(%arg0, %arg2) : (tensor<?x1xf32>, tensor<f32>) -> tensor<?x1xf32>
  %2 = "tf.Mul"(%arg1, %arg3) : (tensor<?x1xf32>, tensor<f32>) -> tensor<?x1xf32>
  %3 = "tf.ConcatV2"(%1, %2, %0) : (tensor<?x1xf32>, tensor<?x1xf32>, tensor<i32>) -> tensor<?x2xf32>

  return %3 : tensor<?x2xf32>
}

// CHECK-LABEL: testConcatCwiseBinaryInvalidInnerDim
func @testConcatCwiseBinaryInvalidInnerDim(%arg0: tensor<?x2xf32>,
  %arg1: tensor<?x2xf32>, %arg2: tensor<f32>, %arg3: tensor<f32>) -> tensor<?x4xf32> {
  // Each individual binary operation has an implicit broadcast that will be
  // lost if we would reorder them with the concat.

  // CHECK: "tf.ConcatV2"(%1, %2, %0)
  %0 = "tf.Const"() { value = dense<1> : tensor<i32> } : () -> tensor<i32>
  %1 = "tf.Mul"(%arg0, %arg2) : (tensor<?x2xf32>, tensor<f32>) -> tensor<?x2xf32>
  %2 = "tf.Mul"(%arg1, %arg3) : (tensor<?x2xf32>, tensor<f32>) -> tensor<?x2xf32>
  %3 = "tf.ConcatV2"(%1, %2, %0) : (tensor<?x2xf32>, tensor<?x2xf32>, tensor<i32>) -> tensor<?x4xf32>

  return %3 : tensor<?x4xf32>
}

// CHECK-LABEL: testLogOfSoftmax
func @testLogOfSoftmax(%arg0: tensor<8x16xf32>) -> tensor<8x16xf32> {
  %0 = "tf.Softmax"(%arg0) : (tensor<8x16xf32>) -> tensor<8x16xf32>
  %1 = "tf.Log"(%0) : (tensor<8x16xf32>) -> tensor<8x16xf32>
  return %1: tensor<8x16xf32>

// CHECK: %0 = "tf.LogSoftmax"(%arg0) : (tensor<8x16xf32>) -> tensor<8x16xf32>
// CHECK: return %0
}

// CHECK-LABEL: testLogToLog1p
func @testLogToLog1p(%arg0 : tensor<4x4xf32>) -> tensor<4x4xf32> {
  %0 = "tf.Const"() {value = dense<1.0> : tensor<f32>} : () -> tensor<1xf32>
  %1 = "tf.Const"() {value = dense<2.0> : tensor<f32>} : () -> tensor<1xf32>
  %2 = "tf.Const"() {value = dense<[1.0, 1.0, 1.0, 1.0]> : tensor<4xf32>} : () -> tensor<4xf32>

  // CHECK: %2 = "tf.Log1p"(%arg0) : (tensor<4x4xf32>) -> tensor<4x4xf32>
  %3 = "tf.AddV2"(%arg0, %0): (tensor<4x4xf32>, tensor<1xf32>) -> tensor<4x4xf32>
  %4 = "tf.Log"(%3): (tensor<4x4xf32>) -> tensor<4x4xf32>

  // CHECK: %3 = "tf.AddV2"
  // CHECK: %4 = "tf.Log"(%3)
  %5 = "tf.AddV2"(%4, %1): (tensor<4x4xf32>, tensor<1xf32>) -> tensor<4x4xf32>
  %6 = "tf.Log"(%5): (tensor<4x4xf32>) -> tensor<4x4xf32>

  // This is a legal canonicalization because constant shape 4xf32 is
  // broadcastable to 4x4xf32, however we currently do not support this case,
  // and canonicalize only if the constant is a scalar.
  // CHECK: %5 = "tf.AddV2"
  // CHECK: %6 = "tf.Log"(%5)
  %7 = "tf.AddV2"(%6, %2): (tensor<4x4xf32>, tensor<4xf32>) -> tensor<4x4xf32>
  %8 = "tf.Log"(%7): (tensor<4x4xf32>) -> tensor<4x4xf32>

  // CHECK: return %6
  return %8: tensor<4x4xf32>
}

// CHECK-LABEL: testSubOfNeg
func @testSubOfNeg(%arg0: tensor<8x16xf32>, %arg1: tensor<8x16xf32>) -> tensor<8x16xf32> {
  %0 = "tf.Neg"(%arg1) : (tensor<8x16xf32>) -> tensor<8x16xf32>
  %1 = "tf.Sub"(%arg0, %0) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xf32>
  return %1: tensor<8x16xf32>

// CHECK: %0 = "tf.AddV2"(%arg0, %arg1) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xf32>
// CHECK: return %0
}

// CHECK-LABEL: testSubOfZero
func @testSubOfZero(%arg0: tensor<?x1xf32>, %arg1: tensor<4x1xf32>) -> (tensor<?x1xf32>, tensor<4x1xf32>) {
  %0 = "tf.Const"() {value = dense<0.0> : tensor<f32>} : () -> tensor<f32>
  %1 = "tf.Sub"(%arg0, %0) : (tensor<?x1xf32>, tensor<f32>) -> tensor<?x1xf32>
  %2 = "tf.Sub"(%arg1, %0) : (tensor<4x1xf32>, tensor<f32>) -> tensor<4x1xf32>
  return %1, %2: tensor<?x1xf32>, tensor<4x1xf32>

// CHECK: return %arg0, %arg1
}

// CHECK-LABEL: testSubOfZeroWithBroadcasting
func @testSubOfZeroWithBroadcasting(%arg0: tensor<4x1xf32>) -> tensor<4x4xf32> {
  // This is an identity arithmetic operation, however we do not currently fold
  // it because it has a broadcasting.
  %0 = "tf.Const"() {value = dense<[[0.0, 0.0, 0.0, 0.0]]> : tensor<1x4xf32>} : () -> tensor<1x4xf32>
  %1 = "tf.Sub"(%arg0, %0) : (tensor<4x1xf32>, tensor<1x4xf32>) -> tensor<4x4xf32>
  return %1 : tensor<4x4xf32>

// CHECK: return %1
}

// CHECK-LABEL: testSquareOfSub
func @testSquareOfSub(%arg0: tensor<8x16xf32>, %arg1: tensor<8x16xf32>) -> tensor<8x16xf32> {
  %0 = "tf.Sub"(%arg0, %arg1) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xf32>
  %1 = "tf.Square"(%0) : (tensor<8x16xf32>) -> tensor<8x16xf32>
  return %1: tensor<8x16xf32>

// CHECK: %0 = "tf.SquaredDifference"(%arg0, %arg1) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xf32>
// CHECK: return %0
}

// CHECK-LABEL: testAddToAddV2
func @testAddToAddV2(%arg0: tensor<8x16xf32>, %arg1: tensor<8x16xf32>) -> tensor<8x16xf32> {
  %0 = "tf.Add"(%arg0, %arg1) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xf32>
  return %0: tensor<8x16xf32>

// CHECK: %0 = "tf.AddV2"(%arg0, %arg1) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xf32>
// CHECK: return %0
}

// CHECK-LABEL: testNoAddToAddV2ForStringType
func @testNoAddToAddV2ForStringType(%arg0: tensor<8x16x!tf.string>, %arg1: tensor<8x16x!tf.string>) -> tensor<8x16x!tf.string> {
  %0 = "tf.Add"(%arg0, %arg1) : (tensor<8x16x!tf.string>, tensor<8x16x!tf.string>) -> tensor<8x16x!tf.string>
  return %0: tensor<8x16x!tf.string>

// CHECK: %0 = "tf.Add"(%arg0, %arg1) : (tensor<8x16x!tf.string>, tensor<8x16x!tf.string>) -> tensor<8x16x!tf.string>
// CHECK: return %0
}

// CHECK-LABEL: testAddOfNegLeft
func @testAddOfNegLeft(%arg0: tensor<8x16xf32>, %arg1: tensor<8x16xf32>) -> tensor<8x16xf32> {
  %0 = "tf.Neg"(%arg0) : (tensor<8x16xf32>) -> tensor<8x16xf32>
  %1 = "tf.Add"(%0, %arg1) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xf32>
  return %1: tensor<8x16xf32>

// CHECK: %0 = "tf.Sub"(%arg1, %arg0) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xf32>
// CHECK: return %0
}

// CHECK-LABEL: testAddOfNegRight
func @testAddOfNegRight(%arg0: tensor<8x16xf32>, %arg1: tensor<8x16xf32>) -> tensor<8x16xf32> {
  %0 = "tf.Neg"(%arg1) : (tensor<8x16xf32>) -> tensor<8x16xf32>
  %1 = "tf.Add"(%arg0, %0) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xf32>
  return %1: tensor<8x16xf32>

// CHECK: %0 = "tf.Sub"(%arg0, %arg1) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xf32>
// CHECK: return %0
}

// CHECK-LABEL: testAddV2OfNegLeft
func @testAddV2OfNegLeft(%arg0: tensor<8x16xf32>, %arg1: tensor<8x16xf32>) -> tensor<8x16xf32> {
  %0 = "tf.Neg"(%arg0) : (tensor<8x16xf32>) -> tensor<8x16xf32>
  %1 = "tf.AddV2"(%0, %arg1) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xf32>
  return %1: tensor<8x16xf32>
// CHECK: %0 = "tf.Sub"(%arg1, %arg0) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xf32>
// CHECK: return %0
}

// CHECK-LABEL: testAddV2OfNegRight
func @testAddV2OfNegRight(%arg0: tensor<8x16xf32>, %arg1: tensor<8x16xf32>) -> tensor<8x16xf32> {
  %0 = "tf.Neg"(%arg1) : (tensor<8x16xf32>) -> tensor<8x16xf32>
  %1 = "tf.AddV2"(%arg0, %0) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xf32>
  return %1: tensor<8x16xf32>

// CHECK: %0 = "tf.Sub"(%arg0, %arg1) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xf32>
// CHECK: return %0
}

// CHECK-LABEL: testAddV2IdentityScalar
func @testAddV2IdentityScalar(%arg0: tensor<f32>, %arg1: tensor<?xf32>, %arg2: tensor<4xf32>) -> (tensor<f32>, tensor<?xf32>, tensor<4xf32>) {
  %0 = "tf.Const"() {value = dense<0.0> : tensor<f32>} : () -> tensor<f32>

  // Identity scalar (0.0) is foldable with operand of any shape because
  // scalar is safely broadcastable to any shape.

  %1 = "tf.AddV2"(%arg0, %0) : (tensor<f32>, tensor<f32>) -> tensor<f32>
  %2 = "tf.AddV2"(%arg1, %0) : (tensor<?xf32>, tensor<f32>) -> tensor<?xf32>
  %3 = "tf.AddV2"(%arg2, %0) : (tensor<4xf32>, tensor<f32>) -> tensor<4xf32>

  %4 = "tf.AddV2"(%0, %1) : (tensor<f32>, tensor<f32>) -> tensor<f32>
  %5 = "tf.AddV2"(%0, %2) : (tensor<f32>, tensor<?xf32>) -> tensor<?xf32>
  %6 = "tf.AddV2"(%0, %3) : (tensor<f32>, tensor<4xf32>) -> tensor<4xf32>

  // CHECK: return %arg0, %arg1, %arg2
  return %4, %5, %6: tensor<f32>, tensor<?xf32>, tensor<4xf32>
}

// CHECK-LABEL: testAddV2IdentityTensor
func @testAddV2IdentityTensor(%arg0: tensor<f32>, %arg1: tensor<4xf32>) -> (tensor<4xf32>, tensor<4xf32>, tensor<4xf32>, tensor<4xf32>) {
  %0 = "tf.Const"() {value = dense<[0.0, 0.0, 0.0, 0.0]> : tensor<4xf32>} : () -> tensor<4xf32>

  // If operand is a scalar, then the identity value (0.0 for addition) can
  // be of any shape, because operand is safely broadcastable to any shape.
  //
  // However we can't fold this arithmetic operation because the operand
  // shape does not match the result shape.

  %1 = "tf.AddV2"(%arg0, %0) : (tensor<f32>, tensor<4xf32>) -> tensor<4xf32>
  %2 = "tf.AddV2"(%0, %arg0) : (tensor<4xf32>, tensor<f32>) -> tensor<4xf32>

  // If operand has the same shape as a result, we can fold it.
  %3 = "tf.AddV2"(%arg1, %0) : (tensor<4xf32>, tensor<4xf32>) -> tensor<4xf32>
  %4 = "tf.AddV2"(%0, %arg1) : (tensor<4xf32>, tensor<4xf32>) -> tensor<4xf32>

  // CHECK: return %1, %2, %arg1, %arg1
  return %1, %2, %3, %4: tensor<4xf32>, tensor<4xf32>, tensor<4xf32>, tensor<4xf32>
}

// CHECK-LABEL: testDoubleConj
func @testDoubleConj(%arg0: tensor<8x16x32x64xcomplex<f32>>) -> tensor<8x16x32x64xcomplex<f32>> {
  %0 = "tf.Conj"(%arg0) : (tensor<8x16x32x64xcomplex<f32>>) -> tensor<8x16x32x64xcomplex<f32>>
  %1 = "tf.Conj"(%0) : (tensor<8x16x32x64xcomplex<f32>>) -> tensor<8x16x32x64xcomplex<f32>>
  return %1: tensor<8x16x32x64xcomplex<f32>>

// CHECK: return %arg0
}

// CHECK-LABEL: testDoubleInvert
func @testDoubleInvert(%arg0: tensor<8x16x32x64xi32>) -> tensor<8x16x32x64xi32> {
  %0 = "tf.Invert"(%arg0) : (tensor<8x16x32x64xi32>) -> tensor<8x16x32x64xi32>
  %1 = "tf.Invert"(%0) : (tensor<8x16x32x64xi32>) -> tensor<8x16x32x64xi32>
  return %1: tensor<8x16x32x64xi32>

// CHECK: return %arg0
}

// CHECK-LABEL: testDoubleLogicalNot
func @testDoubleLogicalNot(%arg0: tensor<8x16x32x64xi1>) -> tensor<8x16x32x64xi1> {
  %0 = "tf.LogicalNot"(%arg0) : (tensor<8x16x32x64xi1>) -> tensor<8x16x32x64xi1>
  %1 = "tf.LogicalNot"(%0) : (tensor<8x16x32x64xi1>) -> tensor<8x16x32x64xi1>
  return %1: tensor<8x16x32x64xi1>

// CHECK: return %arg0
}

// CHECK-LABEL: testDoubleNeg
func @testDoubleNeg(%arg0: tensor<8x16x32x64xi32>) -> tensor<8x16x32x64xi32> {
  %0 = "tf.Neg"(%arg0) : (tensor<8x16x32x64xi32>) -> tensor<8x16x32x64xi32>
  %1 = "tf.Neg"(%0) : (tensor<8x16x32x64xi32>) -> tensor<8x16x32x64xi32>
  return %1: tensor<8x16x32x64xi32>

// CHECK: return %arg0
}

// CHECK-LABEL: testDoubleReciprocal
func @testDoubleReciprocal(%arg0: tensor<8x16x32x64xi32>) -> tensor<8x16x32x64xi32> {
  %0 = "tf.Reciprocal"(%arg0) : (tensor<8x16x32x64xi32>) -> tensor<8x16x32x64xi32>
  %1 = "tf.Reciprocal"(%0) : (tensor<8x16x32x64xi32>) -> tensor<8x16x32x64xi32>
  return %1: tensor<8x16x32x64xi32>

// CHECK: return %arg0
}

// CHECK-LABEL: testRedundantReshape
func @testRedundantReshape(%arg0: tensor<4x4xi32>) -> tensor<2x8xi32> {
  %0 = "tf.Const"() {value = dense<[8, 2]> : tensor<2xi32>} : () -> tensor<2xi32>
  %1 = "tf.Const"() {value = dense<[2, 8]> : tensor<2xi32>} : () -> tensor<2xi32>
  %2 = "tf.Reshape"(%arg0, %0) : (tensor<4x4xi32>, tensor<2xi32>) -> tensor<8x2xi32>
  %3 = "tf.Reshape"(%2, %1) : (tensor<8x2xi32>, tensor<2xi32>) -> tensor<2x8xi32>
  return %3: tensor<2x8xi32>

  // CHECK: %0 = "tf.Const"
  // CHECK-SAME: value = dense<[2, 8]> : tensor<2xi32>
  // CHECK: %1 = "tf.Reshape"(%arg0, %0)
  // CHECK: return %1 : tensor<2x8xi32>
}

// CHECK-LABEL: testReshapeToSelfShape
func @testReshapeToSelfShape(%arg0: tensor<?x4xf32>) -> tensor<?x4xf32> {
  %0 = "tf.Shape"(%arg0) : (tensor<?x4xf32>) -> tensor<2xi32>
  %1 = "tf.Reshape"(%arg0, %0) : (tensor<?x4xf32>, tensor<2xi32>) -> tensor<?x4xf32>

  // CHECK: return %arg0 : tensor<?x4xf32>
  return %1: tensor<?x4xf32>
}

// CHECK-LABEL: func @testReshapeNoOp
func @testReshapeNoOp(%arg0: tensor<2x4xf32>, %arg1: tensor<2xi32>) -> tensor<2x4xf32> {
  %0 = "tf.Reshape"(%arg0, %arg1) : (tensor<2x4xf32>, tensor<2xi32>) -> tensor<2x4xf32>

  // CHECK: return %arg0
  return %0 : tensor<2x4xf32>
}

// CHECK-LABEL: func @testBroadcastToNoOp
func @testBroadcastToNoOp(%arg0: tensor<2x4xf32>, %arg1: tensor<2xi32>) -> tensor<2x4xf32> {
  %0 = "tf.BroadcastTo"(%arg0, %arg1) : (tensor<2x4xf32>, tensor<2xi32>) -> tensor<2x4xf32>

  // CHECK: return %arg0
  return %0 : tensor<2x4xf32>
}

// CHECK-LABEL: func @testPackShapeComputation
func @testPackShapeComputation(%arg0: tensor<?x1xf32>, %arg1: tensor<?x1x2xf32>, %arg2: tensor<*xf32>) -> (tensor<2xi32>, tensor<3xi32>, tensor<3xi32>,  tensor<3xi32>, tensor<3xi32>, tensor<*xi32>) {
  // Test dimensions sizes.
  %d1 = "tf.Const"() {value = dense<1> : tensor<i32>} : () -> tensor<i32>
  %d2 = "tf.Const"() {value = dense<2> : tensor<i32>} : () -> tensor<i32>

  // Slice bounds.
  %0 = "tf.Const"() {value = dense<0> : tensor<1xi32>} : () -> tensor<1xi32>
  %1 = "tf.Const"() {value = dense<1> : tensor<1xi32>} : () -> tensor<1xi32>
  %2 = "tf.Const"() {value = dense<2> : tensor<1xi32>} : () -> tensor<1xi32>

  // Fold pack operation if it computes the input tensor shape:
  //
  //   %shape  = tf.Shape(%arg)                    // [? x ...]
  //   %dim0   = tf.StridedSlice(%shape, 0, 1, 1)  // get unknown dim0 value
  //   %pack   = tf.Pack(dim0, ...) { axis = 0 }   // [? x ...]
  //
  // Where `...` are some statically known dimensions. In this case %pack can be
  // replace with a %shape. This is a common pattern in models with a dynamic
  // batch size.

  // Test Rank 2
  // CHECK: %[[SHAPE0:.*]] = "tf.Shape"
  %3 = "tf.Shape"(%arg0) : (tensor<?x1xf32>) -> tensor<2xi32>
  %4 = "tf.StridedSlice"(%3, %0, %1, %1) {shrink_axis_mask = 1 : i64} : (tensor<2xi32>, tensor<1xi32>, tensor<1xi32>, tensor<1xi32>) -> tensor<i32>
  %5 = "tf.Pack"(%4, %d1) {axis = 0 : i64} : (tensor<i32>, tensor<i32>) -> tensor<2xi32>
  %6 = "tf.Reshape"(%arg0, %5) : (tensor<?x1xf32>, tensor<2xi32>) -> tensor<?x1xf32>

  // Test Rank 3.
  // CHECK: %[[SHAPE1:.*]] = "tf.Shape"
  %7 = "tf.Shape"(%arg1) : (tensor<?x1x2xf32>) -> tensor<3xi32>
  %8 = "tf.StridedSlice"(%7, %0, %1, %1) {shrink_axis_mask = 1 : i64} : (tensor<3xi32>, tensor<1xi32>, tensor<1xi32>, tensor<1xi32>) -> tensor<i32>
  %9 = "tf.Pack"(%8, %d1, %d2) {axis = 0 : i64} : (tensor<i32>, tensor<i32>, tensor<i32>) -> tensor<3xi32>
  %10 = "tf.Reshape"(%arg1, %9) : (tensor<?x1x2xf32>, tensor<3xi32>) -> tensor<?x1x2xf32>

  // Packed dimensions have different order from the reshape operand:
  //   [?, 1, 2] vs [?, 2, 1]
  %14 = "tf.StridedSlice"(%7, %0, %1, %1) {shrink_axis_mask = 1 : i64} : (tensor<3xi32>, tensor<1xi32>, tensor<1xi32>, tensor<1xi32>) -> tensor<i32>
  %15 = "tf.Pack"(%14, %d2, %d1) {axis = 0 : i64} : (tensor<i32>, tensor<i32>, tensor<i32>) -> tensor<3xi32>
  // CHECK: %[[PACK0:.*]] = "tf.Pack"

  // StridedSlice takes second dimension from the shape:
  //   begin = [1], end = [2], stride = [1]
  %17 = "tf.StridedSlice"(%7, %1, %2, %1) {shrink_axis_mask = 1 : i64} : (tensor<3xi32>, tensor<1xi32>, tensor<1xi32>, tensor<1xi32>) -> tensor<i32>
  %18 = "tf.Pack"(%17, %d1, %d2) {axis = 0 : i64} : (tensor<i32>, tensor<i32>, tensor<i32>) -> tensor<3xi32>
  // CHECK: %[[PACK1:.*]] = "tf.Pack"

  // Packed dimensions have higher rank than the reshape operand:
  //   [?, 1] vs [?, 1, 1]
  %20 = "tf.StridedSlice"(%3, %0, %1, %1) {shrink_axis_mask = 1 : i64} : (tensor<2xi32>, tensor<1xi32>, tensor<1xi32>, tensor<1xi32>) -> tensor<i32>
  %21 = "tf.Pack"(%20, %d1, %d1) {axis = 0 : i64} : (tensor<i32>, tensor<i32>, tensor<i32>) -> tensor<3xi32>
  // CHECK: %[[PACK2:.*]] = "tf.Pack"

  // Make sure a dynamic ranked shape doesn't crash the "canonicalize" pass
  %23 = "tf.Shape"(%arg2) : (tensor<*xf32>) -> tensor<*xi32>
  %24 = "tf.StridedSlice"(%23, %0, %1, %1) {shrink_axis_mask = 1 : i64} : (tensor<*xi32>, tensor<1xi32>, tensor<1xi32>, tensor<1xi32>) -> tensor<*xi32>
  %25 = "tf.Pack"(%24, %d1) {axis = 0 : i64} : (tensor<*xi32>, tensor<i32>) -> tensor<*xi32>
  // CHECK: %[[PACK3:.*]] = "tf.Pack"

  // CHECK: return %[[SHAPE0]], %[[SHAPE1]], %[[PACK0]], %[[PACK1]], %[[PACK2]], %[[PACK3]]
  return %5, %9, %15, %18, %21, %25 : tensor<2xi32>, tensor<3xi32>, tensor<3xi32>,  tensor<3xi32>, tensor<3xi32>, tensor<*xi32>
}

// CHECK-LABEL: testSelectScalarPred
func @testSelectScalarPred(%arg0: tensor<i1>, %arg1: tensor<4x2xf16>, %arg2: tensor<4x2xf16>) -> tensor<4x2xf16> {
  // CHECK-NEXT: "tf.SelectV2"(%arg0, %arg1, %arg2) : (tensor<i1>, tensor<4x2xf16>, tensor<4x2xf16>) -> tensor<4x2xf16>
  %0 = "tf.Select"(%arg0, %arg1, %arg2) : (tensor<i1>, tensor<4x2xf16>, tensor<4x2xf16>) -> tensor<4x2xf16>
  return %0: tensor<4x2xf16>
}

// CHECK-LABEL: testSelectVectorPred
func @testSelectVectorPred(%arg0: tensor<2xi1>, %arg1: tensor<2x3xf16>, %arg2: tensor<2x3xf16>) -> tensor<2x3xf16> {
  // CHECK-NEXT: %[[SHAPE:.*]] = "tf.Const"
  // CHECK-NEXT: %[[PRED:.*]] = "tf.Reshape"(%arg0, %[[SHAPE]]) : (tensor<2xi1>, tensor<2xi64>) -> tensor<2x1xi1>
  // CHECK-NEXT: "tf.SelectV2"(%[[PRED]], %arg1, %arg2) : (tensor<2x1xi1>, tensor<2x3xf16>, tensor<2x3xf16>) -> tensor<2x3xf16>
  %0 = "tf.Select"(%arg0, %arg1, %arg2) : (tensor<2xi1>, tensor<2x3xf16>, tensor<2x3xf16>) -> tensor<2x3xf16>
  return %0: tensor<2x3xf16>
}

// CHECK-LABEL: testSelectAllSameShape
func @testSelectAllSameShape(%arg0: tensor<2x3xi1>, %arg1: tensor<2x3xf16>, %arg2: tensor<2x3xf16>) -> tensor<2x3xf16> {
  // CHECK-NEXT: "tf.SelectV2"(%arg0, %arg1, %arg2) : (tensor<2x3xi1>, tensor<2x3xf16>, tensor<2x3xf16>) -> tensor<2x3xf16>
  %0 = "tf.Select"(%arg0, %arg1, %arg2) : (tensor<2x3xi1>, tensor<2x3xf16>, tensor<2x3xf16>) -> tensor<2x3xf16>
  return %0: tensor<2x3xf16>
}

// If we don't have guarantees on input shapes, we can't support canonicalizing
// to SelectV2. Test these cases.
// CHECK-LABEL: testSelectInvalid
func @testSelectInvalid(%arg0: tensor<?xi1>, %arg1: tensor<2x3xf16>, %arg2: tensor<2x3xf16>) -> tensor<2x3xf16> {
  // CHECK-NEXT: tf.Select
  %0 = "tf.Select"(%arg0, %arg1, %arg2) : (tensor<?xi1>, tensor<2x3xf16>, tensor<2x3xf16>) -> tensor<2x3xf16>
  return %0: tensor<2x3xf16>
}

// CHECK-LABEL: testSelectInvalidUnranked
func @testSelectInvalidUnranked(%arg0: tensor<6x7xi1>, %arg1: tensor<*xf16>, %arg2: tensor<*xf16>) -> tensor<*xf16> {
  // CHECK-NEXT: tf.Select
  %0 = "tf.Select"(%arg0, %arg1, %arg2) : (tensor<6x7xi1>, tensor<*xf16>, tensor<*xf16>) -> tensor<*xf16>
  return %0: tensor<*xf16>
}

// CHECK-LABEL: testSelectThenUnranked
func @testSelectThenUnranked(%arg0: tensor<3xi1>, %arg1: tensor<*xf16>, %arg2: tensor<3x2xf16>) -> tensor<*xf16> {
  // CHECK-NEXT: tf.Select
  %0 = "tf.Select"(%arg0, %arg1, %arg2) : (tensor<3xi1>, tensor<*xf16>, tensor<3x2xf16>) -> tensor<*xf16>
  return %0: tensor<*xf16>
}

// CHECK-LABEL: testSelectElseUnranked
func @testSelectElseUnranked(%arg0: tensor<3xi1>, %arg1: tensor<3x2xf16>, %arg2: tensor<*xf16>) -> tensor<*xf16> {
  // CHECK-NEXT: tf.Select
  %0 = "tf.Select"(%arg0, %arg1, %arg2) : (tensor<3xi1>, tensor<3x2xf16>, tensor<*xf16>) -> tensor<*xf16>
  return %0: tensor<*xf16>
}

// CHECK-LABEL: testTileMultiplesAllOnes
func @testTileMultiplesAllOnes(%arg0: tensor<2x3xf32>) -> tensor<2x3xf32> {
  %cst = constant dense <[1, 1]> : tensor<2xi32>
  // CHECK: return %arg0
  %0 = "tf.Tile"(%arg0, %cst) : (tensor<2x3xf32>, tensor<2xi32>) -> tensor<2x3xf32>
  return %0: tensor<2x3xf32>
}

// CHECK-LABEL: testLogicalNotOfEqual
func @testLogicalNotOfEqual(%arg0: tensor<8x16xf32>, %arg1: tensor<8x16xf32>) -> tensor<8x16xi1> {
  %0 = "tf.Equal"(%arg0, %arg1) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xi1>
  %1 = "tf.LogicalNot"(%0) : (tensor<8x16xi1>) -> tensor<8x16xi1>
  return %1: tensor<8x16xi1>

// CHECK: %[[NE:.*]] = "tf.NotEqual"(%arg0, %arg1) {incompatible_shape_error = true}
// CHECK: return %[[NE]]
}

// CHECK-LABEL: testLogicalNotOfNotEqual
func @testLogicalNotOfNotEqual(%arg0: tensor<8x16xf32>, %arg1: tensor<8x16xf32>) -> tensor<8x16xi1> {
  %0 = "tf.NotEqual"(%arg0, %arg1) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xi1>
  %1 = "tf.LogicalNot"(%0) : (tensor<8x16xi1>) -> tensor<8x16xi1>
  return %1: tensor<8x16xi1>

// CHECK: %[[NE:.*]] = "tf.Equal"(%arg0, %arg1) {incompatible_shape_error = true}
// CHECK: return %[[NE]]
}

// CHECK-LABEL: testLogicalNotOfGreater
func @testLogicalNotOfGreater(%arg0: tensor<8x16xf32>, %arg1: tensor<8x16xf32>) -> tensor<8x16xi1> {
  %0 = "tf.Greater"(%arg0, %arg1) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xi1>
  %1 = "tf.LogicalNot"(%0) : (tensor<8x16xi1>) -> tensor<8x16xi1>
  return %1: tensor<8x16xi1>

// CHECK: %0 = "tf.LessEqual"(%arg0, %arg1) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xi1>
// CHECK: return %0
}

// CHECK-LABEL: testLogicalNotOfGreaterEqual
func @testLogicalNotOfGreaterEqual(%arg0: tensor<8x16xf32>, %arg1: tensor<8x16xf32>) -> tensor<8x16xi1> {
  %0 = "tf.GreaterEqual"(%arg0, %arg1) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xi1>
  %1 = "tf.LogicalNot"(%0) : (tensor<8x16xi1>) -> tensor<8x16xi1>
  return %1: tensor<8x16xi1>

// CHECK: %0 = "tf.Less"(%arg0, %arg1) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xi1>
// CHECK: return %0
}

// CHECK-LABEL: testLogicalNotOfLess
func @testLogicalNotOfLess(%arg0: tensor<8x16xf32>, %arg1: tensor<8x16xf32>) -> tensor<8x16xi1> {
  %0 = "tf.Less"(%arg0, %arg1) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xi1>
  %1 = "tf.LogicalNot"(%0) : (tensor<8x16xi1>) -> tensor<8x16xi1>
  return %1: tensor<8x16xi1>

// CHECK: %0 = "tf.GreaterEqual"(%arg0, %arg1) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xi1>
// CHECK: return %0
}

// CHECK-LABEL: testLogicalNotOfLessEqual
func @testLogicalNotOfLessEqual(%arg0: tensor<8x16xf32>, %arg1: tensor<8x16xf32>) -> tensor<8x16xi1> {
  %0 = "tf.LessEqual"(%arg0, %arg1) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xi1>
  %1 = "tf.LogicalNot"(%0) : (tensor<8x16xi1>) -> tensor<8x16xi1>
  return %1: tensor<8x16xi1>

// CHECK: %0 = "tf.Greater"(%arg0, %arg1) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xi1>
// CHECK: return %0
}

// CHECK-LABEL: testSizeFolding
func @testSizeFolding(%arg0: tensor<3x5x7xf32>) -> tensor<i32> {
  %0 = "tf.Size"(%arg0) : (tensor<3x5x7xf32>) -> tensor<i32>
  return %0: tensor<i32>

// CHECK: %0 = "tf.Const"() {value = dense<105> : tensor<i32>} : () -> tensor<i32>
// CHECK: return %0 : tensor<i32>
}

// CHECK-LABEL: testDivWithSqrtDivisor
func @testDivWithSqrtDivisor(%arg0: tensor<8x16xf32>, %arg1: tensor<8x16xf32>) -> tensor<8x16xf32> {
  %0 = "tf.Sqrt"(%arg1) : (tensor<8x16xf32>) -> tensor<8x16xf32>
  %1 = "tf.Div"(%arg0, %0) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xf32>
  return %1: tensor<8x16xf32>

// CHECK: %0 = "tf.Rsqrt"(%arg1) : (tensor<8x16xf32>) -> tensor<8x16xf32>
// CHECK: %1 = "tf.Mul"(%arg0, %0) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xf32>
// CHECK: return %1
}

// CHECK-LABEL: testRealDivWithSqrtDivisor
func @testRealDivWithSqrtDivisor(%arg0: tensor<8x16xf32>, %arg1: tensor<8x16xf32>) -> tensor<8x16xf32> {
  %0 = "tf.Sqrt"(%arg1) : (tensor<8x16xf32>) -> tensor<8x16xf32>
  %1 = "tf.RealDiv"(%arg0, %0) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xf32>
  return %1: tensor<8x16xf32>

// CHECK: %0 = "tf.Rsqrt"(%arg1) : (tensor<8x16xf32>) -> tensor<8x16xf32>
// CHECK: %1 = "tf.Mul"(%arg0, %0) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xf32>
// CHECK: return %1
}

// CHECK-LABEL: testRealDivWithConstDivisor
func @testRealDivWithConstDivisor(%arg0: tensor<8x2xf32>) -> tensor<8x2xf32> {
  %0 = "tf.Const"() {value = dense<[2.0, 4.0]> : tensor<2xf32>} : () -> tensor<2xf32>
  %1 = "tf.RealDiv"(%arg0, %0) : (tensor<8x2xf32>, tensor<2xf32>) -> tensor<8x2xf32>
  return %1: tensor<8x2xf32>

  // CHECK: %0 = "tf.Const"
  // CHECK-SAME: value = dense<[5.000000e-01, 2.500000e-01]
  // CHECK: %1 = "tf.Mul"(%arg0, %0)
  // CHECK: return %1
}

// CHECK-LABEL: testTruncateDivWithSqrtDivisor
func @testTruncateDivWithSqrtDivisor(%arg0: tensor<8x16xf32>, %arg1: tensor<8x16xf32>) -> tensor<8x16xf32> {
  %0 = "tf.Sqrt"(%arg1) : (tensor<8x16xf32>) -> tensor<8x16xf32>
  %1 = "tf.TruncateDiv"(%arg0, %0) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xf32>
  return %1: tensor<8x16xf32>

// CHECK: %0 = "tf.Rsqrt"(%arg1) : (tensor<8x16xf32>) -> tensor<8x16xf32>
// CHECK: %1 = "tf.Mul"(%arg0, %0) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xf32>
// CHECK: return %1
}

// CHECK-LABEL: testXdivyWithSqrtDivisor
func @testXdivyWithSqrtDivisor(%arg0: tensor<8x16xf32>, %arg1: tensor<8x16xf32>) -> tensor<8x16xf32> {
  %0 = "tf.Sqrt"(%arg1) : (tensor<8x16xf32>) -> tensor<8x16xf32>
  %1 = "tf.Xdivy"(%arg0, %0) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xf32>
  return %1: tensor<8x16xf32>

// CHECK: %0 = "tf.Rsqrt"(%arg1) : (tensor<8x16xf32>) -> tensor<8x16xf32>
// CHECK: %1 = "tf.MulNoNan"(%0, %arg0) : (tensor<8x16xf32>, tensor<8x16xf32>) -> tensor<8x16xf32>
// CHECK: return %1
}

// CHECK-LABEL: @identityTranspose
func @identityTranspose(%arg0: tensor<2x3x4x5x6xf32>) -> tensor<2x3x4x5x6xf32> {
  %0 = "tf.Const"() {value = dense<[0, 1, 2, 3, 4]> : tensor<5xi32>} : () -> tensor<5xi32>
  %1 = "tf.Transpose"(%arg0, %0) : (tensor<2x3x4x5x6xf32>, tensor<5xi32>) -> tensor<2x3x4x5x6xf32>

  return %1 : tensor<2x3x4x5x6xf32>
  // CHECK: return %arg0
}

// CHECK-LABEL: @identityTransposeConst
func @identityTransposeConst(%arg0: tensor<2x3x4x5x6xf32>) -> tensor<2x3x4x5x6xf32> {
  %0 = constant dense<[0, 1, 2, 3, 4]> : tensor<5xi32>
  %1 = "tf.Transpose"(%arg0, %0) : (tensor<2x3x4x5x6xf32>, tensor<5xi32>) -> tensor<2x3x4x5x6xf32>

  return %1 : tensor<2x3x4x5x6xf32>
  // CHECK: return %arg0
}

// CHECK-LABEL: @nonIdentityTranspose
func @nonIdentityTranspose(%arg0: tensor<2x3x4x5x6xf32>) -> tensor<2x3x4x6x5xf32> {
  %0 = "tf.Const"() {value = dense<[0, 1, 2, 4, 3]> : tensor<5xi32>} : () -> tensor<5xi32>
  %1 = "tf.Transpose"(%arg0, %0) : (tensor<2x3x4x5x6xf32>, tensor<5xi32>) -> tensor<2x3x4x6x5xf32>

  return %1 : tensor<2x3x4x6x5xf32>
  // CHECK: %0 = "tf.Const"() {value = dense<[0, 1, 2, 4, 3]> : tensor<5xi32>} : () -> tensor<5xi32>
  // CHECK: %1 = "tf.Transpose"(%arg0, %0) : (tensor<2x3x4x5x6xf32>, tensor<5xi32>) -> tensor<2x3x4x6x5xf32>
  // CHECK: return %1
}

// CHECK-LABEL: @cancellableTranspose
func @cancellableTranspose(%arg0: tensor<1x4x4x8xf32>) -> tensor<1x4x4x8xf32> {
  %0 = "tf.Const"() {value = dense<[0, 3, 1, 2]> : tensor<4xi32>} : () -> tensor<4xi32>
  %1 = "tf.Const"() {value = dense<[0, 2, 3, 1]> : tensor<4xi32>} : () -> tensor<4xi32>
  %2 = "tf.Transpose"(%arg0, %0) : (tensor<1x4x4x8xf32>, tensor<4xi32>) -> tensor<1x8x4x4xf32>
  %3 = "tf.Transpose"(%2, %1) : (tensor<1x8x4x4xf32>, tensor<4xi32>) -> tensor<1x4x4x8xf32>

  return %3 : tensor<1x4x4x8xf32>
  // CHECK: return %arg0
}

// CHECK-LABEL: @cancellableTransposeConst
func @cancellableTransposeConst(%arg0: tensor<1x4x4x8xf32>) -> tensor<1x4x4x8xf32> {
  %0 = constant dense<[0, 3, 1, 2]> : tensor<4xi32>
  %1 = constant dense<[0, 2, 3, 1]> : tensor<4xi32>
  %2 = "tf.Transpose"(%arg0, %0) : (tensor<1x4x4x8xf32>, tensor<4xi32>) -> tensor<1x8x4x4xf32>
  %3 = "tf.Transpose"(%2, %1) : (tensor<1x8x4x4xf32>, tensor<4xi32>) -> tensor<1x4x4x8xf32>

  return %3 : tensor<1x4x4x8xf32>
  // CHECK: return %arg0
}

// CHECK-LABEL: @nonCancellableTranspose
func @nonCancellableTranspose(%arg0: tensor<1x4x4x8xf32>) -> tensor<4x1x4x8xf32> {
  %0 = "tf.Const"() {value = dense<[0, 3, 1, 2]> : tensor<4xi32>} : () -> tensor<4xi32>
  %1 = "tf.Const"() {value = dense<[2, 0, 3, 1]> : tensor<4xi32>} : () -> tensor<4xi32>
  %2 = "tf.Transpose"(%arg0, %0) : (tensor<1x4x4x8xf32>, tensor<4xi32>) -> tensor<1x8x4x4xf32>
  %3 = "tf.Transpose"(%2, %1) : (tensor<1x8x4x4xf32>, tensor<4xi32>) -> tensor<4x1x4x8xf32>

  return %3 : tensor<4x1x4x8xf32>
  // CHECK: return %3
}

// CHECK-LABEL: func @addN
func @addN(%arg0: tensor<*xf32>) -> tensor<*xf32> {
  // CHECK: return %arg0
  %0 = "tf.AddN"(%arg0) : (tensor<*xf32>) -> tensor<*xf32>
  return %0 : tensor<*xf32>
}

// CHECK-LABEL: func @ToBool_0DScalarI1
func @ToBool_0DScalarI1(%arg0: tensor<i1>) -> tensor<i1> {
  // CHECK: return %arg0
  %0 = "tf.ToBool"(%arg0) : (tensor<i1>) -> tensor<i1>
  return %0 : tensor<i1>
}

// CHECK-LABEL: func @ToBool_0DScalarInt
func @ToBool_0DScalarInt(%arg0: tensor<i32>) -> tensor<i1> {
  // CHECK: [[Zero:%.*]] = "tf.Const"() {value = dense<0> : tensor<i32>}
  // CHECK: [[NE:%.*]] = "tf.NotEqual"(%arg0, [[Zero]])
  // CHECK: return [[NE]]
  %0 = "tf.ToBool"(%arg0) : (tensor<i32>) -> tensor<i1>
  return %0 : tensor<i1>
}

// CHECK-LABEL: func @ToBool_0DScalarFloat
func @ToBool_0DScalarFloat(%arg0: tensor<f32>) -> tensor<i1> {
  // CHECK: [[Zero:%.*]] = "tf.Const"() {value = dense<0.000000e+00> : tensor<f32>} : () -> tensor<f32>
  // CHECK: [[NE:%.*]] = "tf.NotEqual"(%arg0, [[Zero]])
  // CHECK: return [[NE]]
  %0 = "tf.ToBool"(%arg0) : (tensor<f32>) -> tensor<i1>
  return %0 : tensor<i1>
}

// CHECK-LABEL: func @ToBool_0DScalarString
func @ToBool_0DScalarString(%arg0: tensor<!tf.string>) -> tensor<i1> {
  // CHECK: [[EmptyStr:%.*]] = "tf.Const"() {value = dense<""> : tensor<!tf.string>} : () -> tensor<!tf.string>
  // CHECK: [[NE:%.*]] = "tf.NotEqual"(%arg0, [[EmptyStr]]) {incompatible_shape_error = false} : (tensor<!tf.string>, tensor<!tf.string>) -> tensor<i1>
  // CHECK: return [[NE]] : tensor<i1>
  %0 = "tf.ToBool"(%arg0) : (tensor<!tf.string>) -> tensor<i1>
  return %0 : tensor<i1>
}

// CHECK-LABEL: func @ToBool_1DTensor
func @ToBool_1DTensor(%arg0: tensor<1xf32>) -> tensor<i1> {
  // CHECK: [[Const:%.*]] = "tf.Const"() {value = dense<true> : tensor<i1>} : () -> tensor<i1>
  // CHECK: return [[Const]]
  %0 = "tf.ToBool"(%arg0) : (tensor<1xf32>) -> tensor<i1>
  return %0 : tensor<i1>
}

// CHECK-LABEL: func @ToBool_1DTensorZeroDim
func @ToBool_1DTensorZeroDim(%arg0: tensor<0xf32>) -> tensor<i1> {
  // CHECK: [[Const:%.*]] = "tf.Const"() {value = dense<false> : tensor<i1>} : () -> tensor<i1>
  // CHECK: return [[Const]]
  %0 = "tf.ToBool"(%arg0) : (tensor<0xf32>) -> tensor<i1>
  return %0 : tensor<i1>
}

// CHECK-LABEL: func @ToBool_2DTensor
func @ToBool_2DTensor(%arg0: tensor<1x5xf32>) -> tensor<i1> {
  // CHECK: [[Const:%.*]] = "tf.Const"() {value = dense<true> : tensor<i1>} : () -> tensor<i1>
  // CHECK: return [[Const]]
  %0 = "tf.ToBool"(%arg0) : (tensor<1x5xf32>) -> tensor<i1>
  return %0 : tensor<i1>
}

// CHECK-LABEL: func @ToBool_2DTensorZeroDim
func @ToBool_2DTensorZeroDim(%arg0: tensor<1x0xf32>) -> tensor<i1> {
  // CHECK: [[Const:%.*]] = "tf.Const"() {value = dense<false> : tensor<i1>} : () -> tensor<i1>
  // CHECK: return [[Const]]
  %0 = "tf.ToBool"(%arg0) : (tensor<1x0xf32>) -> tensor<i1>
  return %0 : tensor<i1>
}

// CHECK-LABEL: testReadVariableOpOfCast
func @testReadVariableOpOfCast(%arg0: tensor<!tf.resource<tensor<8x40xf32>>>) -> tensor<8x40xf32> {
  %0 = "tf.Cast"(%arg0) : (tensor<!tf.resource<tensor<8x40xf32>>>) -> tensor<*x!tf.resource>
  %1 = "tf.ReadVariableOp"(%0) : (tensor<*x!tf.resource>) -> tensor<8x40xf32>
  return %1: tensor<8x40xf32>

// CHECK: %0 = "tf.ReadVariableOp"(%arg0) : (tensor<!tf.resource<tensor<8x40xf32>>>) -> tensor<8x40xf32>
// CHECK: return %0
}

// CHECK-LABEL: testReadVariableOpOfCastWithTruncate
func @testReadVariableOpOfCastWithTruncate(%arg0: tensor<!tf.resource<tensor<8x40xf32>>>) -> tensor<8x40xf32> {
  %0 = "tf.Cast"(%arg0) {Truncate = true} : (tensor<!tf.resource<tensor<8x40xf32>>>) -> tensor<*x!tf.resource>
  %1 = "tf.ReadVariableOp"(%0) : (tensor<*x!tf.resource>) -> tensor<8x40xf32>
  return %1: tensor<8x40xf32>

// CHECK: %0 = "tf.ReadVariableOp"(%arg0) : (tensor<!tf.resource<tensor<8x40xf32>>>) -> tensor<8x40xf32>
// CHECK: return %0
}

// CHECK-LABEL: testReadVariableOpOfCastMultiUse
func @testReadVariableOpOfCastMultiUse(%arg0: tensor<!tf.resource<tensor<f32>>>) -> tensor<f32> {
  %0 = "tf.Cast"(%arg0) {Truncate = false} : (tensor<!tf.resource<tensor<f32>>>) -> tensor<*x!tf.resource>
  %1 = "tf.ReadVariableOp"(%0) : (tensor<*x!tf.resource>) -> tensor<f32>
  "tf.AssignVariableOp"(%0, %1) : (tensor<*x!tf.resource>, tensor<f32>) -> ()
  return %1: tensor<f32>

 // CHECK: %0 = "tf.Cast"(%arg0) {Truncate = false} : (tensor<!tf.resource<tensor<f32>>>) -> tensor<*x!tf.resource>
 // CHECK: %1 = "tf.ReadVariableOp"(%0) : (tensor<*x!tf.resource>) -> tensor<f32>
 // CHECK: "tf.AssignVariableOp"(%0, %1) : (tensor<*x!tf.resource>, tensor<f32>) -> ()
 // CHECK: return %1
}

// CHECK-LABEL: testMultiReadVariableOpsOfCast
func @testMultiReadVariableOpsOfCast(%arg0: tensor<!tf.resource<tensor<f32>>>) -> (tensor<f32>, tensor<f32>) {
  %0 = "tf.Cast"(%arg0) {Truncate = false} : (tensor<!tf.resource<tensor<f32>>>) -> tensor<*x!tf.resource>
  %1 = "tf.ReadVariableOp"(%0) : (tensor<*x!tf.resource>) -> tensor<f32>
  %2 = "tf.ReadVariableOp"(%0) : (tensor<*x!tf.resource>) -> tensor<f32>
  return %1, %2: tensor<f32>, tensor<f32>

 // CHECK: %0 = "tf.ReadVariableOp"(%arg0) : (tensor<!tf.resource<tensor<f32>>>) -> tensor<f32>
 // CHECK: %1 = "tf.ReadVariableOp"(%arg0) : (tensor<!tf.resource<tensor<f32>>>) -> tensor<f32>
 // CHECK: return %0, %1
}

// CHECK-LABEL: testRankOfRankedTensor
func @testRankOfRankedTensor(%arg0 : tensor<4x3x2xf32>) -> tensor<i32> {
  // CHECK:[[VAL0:%.+]] = "tf.Const"() {value = dense<3> : tensor<i32>}
  %0 = "tf.Rank"(%arg0) : (tensor<4x3x2xf32>) -> tensor<i32>

  // CHECK: return [[VAL0]]
  return %0 : tensor<i32>
}

// CHECK-LABEL: @foldFill
func @foldFill() -> (tensor<3x2x1xf32>, tensor<*xf32>, tensor<*xcomplex<f32>>) {
  %0 = "tf.Const"() {value = dense<[3, 2, 1]> : tensor<3xi32>} : () -> tensor<3xi32>
  %1 = "tf.Const"() {value = dense<23.0> : tensor<f32>} : () -> tensor<f32>
  // CHECK: "tf.Const"() {value = dense<2.300000e+01> : tensor<3x2x1xf32>}
  %2 = "tf.Fill"(%0, %1) : (tensor<3xi32>, tensor<f32>) -> tensor<3x2x1xf32>
  // CHECK: "tf.Const"() {value = dense<2.300000e+01> : tensor<3x2x1xf32>}
  %3 = "tf.Fill"(%0, %1) : (tensor<3xi32>, tensor<f32>) -> tensor<*xf32>

  %complex_cst = "tf.Const"() {value = dense<(0.000000e+00,1.000000e+00)> : tensor<complex<f32>>} : () -> tensor<complex<f32>>
  // Here, custom folder doesn't handle complex dtypes and it is folded through
  // the constant folding hook.
  // TODO(hinsu): Handle complex dtypes in the custom folder for FillOp.
  // CHECK: "tf.Const"() {value = dense<(0.000000e+00,1.000000e+00)> : tensor<3x2x1xcomplex<f32>>} : () -> tensor<*xcomplex<f32>>
  %4 = "tf.Fill"(%0, %complex_cst) : (tensor<3xi32>, tensor<complex<f32>>) -> tensor<*xcomplex<f32>>

  return %2, %3, %4 : tensor<3x2x1xf32>, tensor<*xf32>, tensor<*xcomplex<f32>>
}

// CHECK-LABEL: foldIf
func @foldIf(%arg0: tensor<f32>, %arg1: tensor<f32>, %arg2: tensor<i1>) -> (tensor<f32>) {
  %0 = "tf.Const"() {value = dense<false> : tensor<i1>} : () -> tensor<i1>
  %1 = "tf.Const"() {value = dense<true> : tensor<i1>} : () -> tensor<i1>

  // CHECK: %0 = "tf.PartitionedCall"(%arg0, %arg1)
  // CHECK-SAME: device = "noodle"
  // CHECK-SAME: f = @sub
  %2 = "tf.If"(%0, %arg0, %arg1) {then_branch = @add, else_branch = @sub, output_shapes = [#tf.shape<>], device = "noodle", is_stateless = true} : (tensor<i1>, tensor<f32>, tensor<f32>) -> tensor<f32>
  // CHECK: %1 = "tf.StatefulPartitionedCall"(%0, %arg1)
  // CHECK-SAME: _underscore_attr = "something"
  // CHECK-SAME: f = @add
  %3 = "tf.If"(%1, %2, %arg1) {then_branch = @add, else_branch = @sub, output_shapes = [#tf.shape<>], _underscore_attr = "something", is_stateless = false} : (tensor<i1>, tensor<f32>, tensor<f32>) -> tensor<f32>

  // CHECK: %2 = "tf.If"
  %4 = "tf.If"(%arg2, %3, %arg1) {then_branch = @add, else_branch = @sub, is_stateless = false} : (tensor<i1>, tensor<f32>, tensor<f32>) -> tensor<f32>

  // CHECK: return %2
  return %4 : tensor<f32>
}

// CHECK-LABEL: foldIfRegion
func @foldIfRegion(%arg0: tensor<f32>, %arg1: tensor<f32>, %arg2: tensor<i1>) -> (tensor<f32>, tensor<f32>) {
  %false = "tf.Const"() {value = dense<false> : tensor<i1>} : () -> tensor<i1>
  %true = "tf.Const"() {value = dense<true> : tensor<i1>} : () -> tensor<i1>

  // CHECK: [[Val0:%.*]] = "tf.Mul"(%arg0, %arg1)
  %0 = "tf.IfRegion"(%true) ({
      %true_value = "tf.Mul"(%arg0, %arg1) : (tensor<f32>, tensor<f32>) -> tensor<f32>
      "tf.Yield"(%true_value) : (tensor<f32>) -> ()
    }, {
      %false_value = "tf.Sub"(%arg0, %arg1) : (tensor<f32>, tensor<f32>) -> tensor<f32>
      "tf.Yield"(%false_value) : (tensor<f32>) -> ()
    }) { is_stateless = true}: (tensor<i1>) -> tensor<f32>

  // CHECK: [[Val1:%.*]] = "tf.Sub"(%arg0, %arg1)
  %1 = "tf.IfRegion"(%false) ({
      %true_value = "tf.Mul"(%arg0, %arg1) : (tensor<f32>, tensor<f32>) -> tensor<f32>
      "tf.Yield"(%true_value) : (tensor<f32>) -> ()
    }, {
      %false_value = "tf.Sub"(%arg0, %arg1) : (tensor<f32>, tensor<f32>) -> tensor<f32>
      "tf.Yield"(%false_value) : (tensor<f32>) -> ()
    }) { is_stateless = true}: (tensor<i1>) -> tensor<f32>

  // CHECK: return [[Val0]], [[Val1]]
  return %0, %1 : tensor<f32>, tensor<f32>
}

// CHECK-LABEL: foldIfRegionMismatchedTypes
func @foldIfRegionMismatchedTypes(%arg0: tensor<?xf32>, %arg1: tensor<?xf32>, %arg2: tensor<i1>) -> tensor<1xf32> {
  %false = "tf.Const"() {value = dense<false> : tensor<i1>} : () -> tensor<i1>
  %true = "tf.Const"() {value = dense<true> : tensor<i1>} : () -> tensor<i1>

  // CHECK: [[Val0:%.*]] = "tf.Mul"(%arg0, %arg1)
  // CHECK-NEXT: [[Cast:%.*]] = "tf.Cast"([[Val0]])
  // CHECK-NEXT: return [[Cast]]
  %0 = "tf.IfRegion"(%true) ({
      %true_value = "tf.Mul"(%arg0, %arg1) : (tensor<?xf32>, tensor<?xf32>) -> tensor<?xf32>
      "tf.Yield"(%true_value) : (tensor<?xf32>) -> ()
    }, {
      %false_value = "tf.Sub"(%arg0, %arg1) : (tensor<?xf32>, tensor<?xf32>) -> tensor<?xf32>
      "tf.Yield"(%false_value) : (tensor<?xf32>) -> ()
    }) { is_stateless = true}: (tensor<i1>) -> tensor<1xf32>
  return %0 : tensor<1xf32>
}

// CHECK-LABEL: func @eliminatePassThroughIfRegion(
// CHECK-SAME:    %[[ARG0:.*]]: tensor<f32>, %[[ARG1:.*]]: tensor<f32>, %[[ARG2:.*]]: tensor<!tf.resource>
func @eliminatePassThroughIfRegion(%arg0: tensor<f32>, %arg1: tensor<f32>, %arg2: tensor<!tf.resource>) -> (tensor<f32>) {
  // CHECK: %[[PRED:.*]] = "tf._SomeOp"() : () -> tensor<i1>
  %pred = "tf._SomeOp"() : () -> tensor<i1>
  // CHECK: %[[IF_OUTPUT:.*]] = "tf.IfRegion"(%[[PRED]]) ( {
  // CHECK:   %[[MUL:.*]] = "tf.Mul"(%[[ARG0]], %[[ARG1]])
  // CHECK:   "tf.Yield"(%[[MUL]]) : (tensor<f32>)
  // CHECK:  },  {
  // CHECK:    %[[SUB:.*]] = "tf.Sub"(%[[ARG0]], %[[ARG1]])
  // CHECK:    "tf.Yield"(%[[SUB]]) : (tensor<f32>)
  // CHECK:  }) {is_stateless = true} : (tensor<i1>) -> tensor<f32>
  %0:4 = "tf.IfRegion"(%pred) ({
      %true_value = "tf.Mul"(%arg0, %arg1) : (tensor<f32>, tensor<f32>) -> tensor<f32>
      "tf.Yield"(%arg1, %arg2, %true_value, %arg2) : (tensor<f32>, tensor<!tf.resource>, tensor<f32>, tensor<!tf.resource>) -> ()
    }, {
      %false_value = "tf.Sub"(%arg0, %arg1) : (tensor<f32>, tensor<f32>) -> tensor<f32>
      "tf.Yield"(%arg1, %arg2, %false_value, %arg2) : (tensor<f32>, tensor<!tf.resource>, tensor<f32>, tensor<!tf.resource>) -> ()
    }) { is_stateless = true}: (tensor<i1>) -> (tensor<f32>, tensor<!tf.resource>, tensor<f32>, tensor<!tf.resource>)
  // CHECK: "tf._SomeOp"(%[[ARG2]], %[[ARG1]]) : (tensor<!tf.resource>, tensor<f32>) -> ()
  "tf._SomeOp"(%0#1, %0#0) : (tensor<!tf.resource>, tensor<f32>) -> ()
  // CHECK: "tf._SomeOp"(%[[ARG2]], %[[IF_OUTPUT]]) : (tensor<!tf.resource>, tensor<f32>) -> ()
  "tf._SomeOp"(%0#3, %0#2) : (tensor<!tf.resource>, tensor<f32>) -> ()
  // CHECK: return %[[IF_OUTPUT]] : tensor<f32>
  return %0#2 : tensor<f32>
}

// CHECK-LABEL: func @eliminatePassThroughCaseRegion(
// CHECK-SAME:    %[[ARG0:.*]]: tensor<f32>, %[[ARG1:.*]]: tensor<f32>, %[[ARG2:.*]]: tensor<!tf.resource>
func @eliminatePassThroughCaseRegion(%arg0: tensor<f32>, %arg1: tensor<f32>, %arg2: tensor<!tf.resource>) -> (tensor<f32>) {
  // CHECK: %[[INDEX:.*]] = "tf._SomeOp"() : () -> tensor<i32>
  %index = "tf._SomeOp"() : () -> tensor<i32>
  // CHECK: %[[CASE_OUTPUT:.*]] = "tf.CaseRegion"(%[[INDEX]]) ( {
  // CHECK:   %[[MUL:.*]] = "tf.Mul"(%[[ARG0]], %[[ARG1]])
  // CHECK:   "tf.Yield"(%[[MUL]]) : (tensor<f32>)
  // CHECK:  },  {
  // CHECK:    %[[SUB:.*]] = "tf.Sub"(%[[ARG0]], %[[ARG1]])
  // CHECK:    "tf.Yield"(%[[SUB]]) : (tensor<f32>)
  // CHECK:  },  {
  // CHECK:    %[[ADD:.*]] = "tf.AddV2"(%[[ARG0]], %[[ARG1]])
  // CHECK:    "tf.Yield"(%[[ADD]]) : (tensor<f32>)
  // CHECK:  }) {is_stateless = true} : (tensor<i32>) -> tensor<f32>
  %0:3 = "tf.CaseRegion"(%index) ({
      %mul = "tf.Mul"(%arg0, %arg1) : (tensor<f32>, tensor<f32>) -> tensor<f32>
      "tf.Yield"(%arg1, %mul, %arg2) : (tensor<f32>, tensor<f32>, tensor<!tf.resource>) -> ()
    }, {
      %sub = "tf.Sub"(%arg0, %arg1) : (tensor<f32>, tensor<f32>) -> tensor<f32>
      "tf.Yield"(%arg1, %sub, %arg2) : (tensor<f32>, tensor<f32>, tensor<!tf.resource>) -> ()
    }, {
      %add = "tf.AddV2"(%arg0, %arg1) : (tensor<f32>, tensor<f32>) -> tensor<f32>
      "tf.Yield"(%arg1, %add, %arg2) : (tensor<f32>, tensor<f32>, tensor<!tf.resource>) -> ()
    }) { is_stateless = true}: (tensor<i32>) -> (tensor<f32>, tensor<f32>, tensor<!tf.resource>)
  // CHECK: "tf._SomeOp"(%[[ARG2]], %[[ARG1]]) : (tensor<!tf.resource>, tensor<f32>) -> ()
  "tf._SomeOp"(%0#2, %0#0) : (tensor<!tf.resource>, tensor<f32>) -> ()
  // CHECK: return %[[CASE_OUTPUT]] : tensor<f32>
  return %0#1 : tensor<f32>
}


// CHECK-LABEL: foldCase
func @foldCase(%arg0: tensor<f32>, %arg1: tensor<f32>) -> (tensor<f32>) {
  %2 = constant dense<1> : tensor<i32>
  %3 = constant dense<0> : tensor<i32>

  // CHECK: PartitionedCall
  // CHECK-SAME: device = "noodle"
  // CHECK-SAME: f = @add
  %4 = "tf.Case"(%2, %arg0, %arg1) {branches = [@sub, @add], output_shapes = [#tf.shape<>], device = "noodle", is_stateless = false} : (tensor<i32>, tensor<f32>, tensor<f32>) -> tensor<f32>
  // CHECK: PartitionedCall
  // CHECK-SAME: _cluster_launch = "not_ready"
  // CHECK-SAME: f = @sub
  %5 = "tf.Case"(%3, %4, %arg1) {branches = [@sub, @add], output_shapes = [#tf.shape<>], _cluster_launch = "not_ready", is_stateless = false} : (tensor<i32>, tensor<f32>, tensor<f32>) -> tensor<f32>
  return %5 : tensor<f32>
}

func @add(%arg0: tensor<*xf32>, %arg1: tensor<*xf32>) -> tensor<*xf32> {
  %0 = "tf.Add"(%arg0, %arg1): (tensor<*xf32>, tensor<*xf32>) -> tensor<*xf32>
  return %0 : tensor<*xf32>
}

func @sub(%arg0: tensor<*xf32>, %arg1: tensor<*xf32>) -> tensor<*xf32> {
  %0 = "tf.Sub"(%arg0, %arg1) : (tensor<*xf32>, tensor<*xf32>) -> tensor<*xf32>
  return %0 : tensor<*xf32>
}

// CHECK-LABEL: testBatchToSpaceToBatchToSpaceND
// CHECK-SAME: ([[INPUT:%.*]]: tensor<?x?x?x?xf32>, [[CROPS:%.*]]: tensor<?x?xi32>)
func @testBatchToSpaceToBatchToSpaceND(%arg0: tensor<?x?x?x?xf32>, %arg1: tensor<?x?xi32>) -> tensor<*xf32> {
  // CHECK: [[BLOCK_SHAPE:%.*]] = "tf.Const"() {value = dense<8> : tensor<2xi64>}
  // CHECK: [[BATCH_TO_SHAPE_ND:%.*]] = "tf.BatchToSpaceND"([[INPUT]], [[BLOCK_SHAPE]], [[CROPS]])
  %0 = "tf.BatchToSpace"(%arg0, %arg1) {block_size = 8 : i64} : (tensor<?x?x?x?xf32>, tensor<?x?xi32>) -> tensor<*xf32>
  // CHECK: return [[BATCH_TO_SHAPE_ND]]
  return %0 : tensor<*xf32>
}

// CHECK-LABEL: testBatchToSpaceDynamicInput
func @testBatchToSpaceDynamicInput(%arg0: tensor<*xf32>, %arg1: tensor<?x?xi32>) -> tensor<*xf32> {
  // CHECK-NOT: "tf.BatchToSpaceND"
  %0 = "tf.BatchToSpace"(%arg0, %arg1) {block_size = 8 : i64} : (tensor<*xf32>, tensor<?x?xi32>) -> tensor<*xf32>
  return %0 : tensor<*xf32>
}

// CHECK-LABEL: testBatchToSpaceDynamicCrops
func @testBatchToSpaceDynamicCrops(%arg0: tensor<?x?x?x?xf32>, %arg1: tensor<*xi32>) -> tensor<*xf32> {
  // CHECK-NOT: "tf.BatchToSpaceND"
  %0 = "tf.BatchToSpace"(%arg0, %arg1) {block_size = 8 : i64} : (tensor<?x?x?x?xf32>, tensor<*xi32>) -> tensor<*xf32>
  return %0 : tensor<*xf32>
}

// CHECK-LABEL: @erase_tf_var_is_initialized
func @erase_tf_var_is_initialized(%arg0 : tensor<!tf.resource<tensor<f32>>>) -> tensor<i1> {
  %vh = "tf.VarHandleOp"() {container = "", shape = "tfshape$", shared_name = "x"} : () -> tensor<!tf.resource<tensor<f32>>>
  %is = "tf.VarIsInitializedOp"(%vh) : (tensor<!tf.resource<tensor<f32>>>) -> tensor<i1>
  %res = "tf.UnknownOp"(%vh) : (tensor<!tf.resource<tensor<f32>>>) -> tensor<i1>
  return %res : tensor<i1>
}
// Unused VarIsInitializedOp is erased.
// CHECK: tf.VarHandleOp
// CHECK-NEXT: tf.UnknownOp


// Simple pass through value
// CHECK-LABEL: testWhileRegionSimplePassThrough
func @testWhileRegionSimplePassThrough(%arg0 : tensor<*xf32>, %arg1 : tensor<i32>) -> tensor<*xf32> {
  // CHECK: "tf.WhileRegion"(%arg1)
  %0:2 = "tf.WhileRegion"(%arg0, %arg1) (
    {
      // condition, check if count has reached 0
      ^bb0(%carg0: tensor<*xf32>, %carg1: tensor<i32>):
      %zero = constant dense<0> : tensor<i32>
      %ne = "tf.NotEqual"(%carg1, %zero) : (tensor<i32>, tensor<i32>) -> tensor<i1>
      "tf.Yield"(%ne) : (tensor<i1>) -> ()
    },
    {
      // loop body
      ^bb0(%barg0: tensor<*xf32>, %barg1: tensor<i32>):
      %one = constant dense<1> : tensor<i32>
      %sub = "tf.Sub"(%barg1, %one) : (tensor<i32>, tensor<i32>) -> tensor<i32>
      "tf.Yield"(%barg0, %sub) : (tensor<*xf32>, tensor<i32>) -> ()
    }
  ) { is_stateless = false } : (tensor<*xf32>, tensor<i32>) -> (tensor<*xf32>, tensor<i32>)
  // CHECK: return %arg0 : tensor<*xf32>
  return %0#0 : tensor<*xf32>
}

// Multiple pass through values
// CHECK-LABEL: testWhileRegionMultiplePassThrough
func @testWhileRegionMultiplePassThrough(%arg0 : tensor<*xf32>, %arg1 : tensor<*xf32>, %arg2 : tensor<*xf32>, %arg3 : tensor<i32>) -> tensor<*xf32> {
  // Verify that first 3 operands are elimiinated.
  // CHECK: "tf.WhileRegion"(%arg3)
  %0:4 = "tf.WhileRegion"(%arg0, %arg1, %arg2, %arg3) (
    {
      // condition, check if count has reached 0
      ^bb0(%carg0 : tensor<*xf32>, %carg1 : tensor<*xf32>, %carg2 : tensor<*xf32>, %carg3 : tensor<i32>):
      %zero = constant dense<0> : tensor<i32>
      %ne = "tf.NotEqual"(%carg3, %zero) : (tensor<i32>, tensor<i32>) -> tensor<i1>
      "tf.Yield"(%ne) : (tensor<i1>) -> ()
    },
    {
      // loop body
      ^bb0(%barg0 : tensor<*xf32>, %barg1 : tensor<*xf32>, %barg2 : tensor<*xf32>, %barg3 : tensor<i32>):
      %one = constant dense<1> : tensor<i32>
      %sub = "tf.Sub"(%barg3, %one) : (tensor<i32>, tensor<i32>) -> tensor<i32>
      "tf.Yield"(%barg0, %barg1, %barg2, %sub) : (tensor<*xf32>, tensor<*xf32>, tensor<*xf32>, tensor<i32>) -> ()
    }
  ) { is_stateless = false } : (tensor<*xf32>, tensor<*xf32>, tensor<*xf32>, tensor<i32>) -> (tensor<*xf32>, tensor<*xf32>, tensor<*xf32>, tensor<i32>)

  // CHECK: %[[SUB0:.*]] = "tf.Sub"(%arg0, %arg1)
  // CHECK: %[[SUB1:.*]] = "tf.Sub"(%arg2, %[[SUB0]])
  // CHECK: return %[[SUB1]]
  %sub0 = "tf.Sub" (%0#0, %0#1) : (tensor<*xf32>, tensor<*xf32>) -> tensor<*xf32>
  %sub1 = "tf.Sub" (%0#2, %sub0) : (tensor<*xf32>, tensor<*xf32>) -> tensor<*xf32>
  return %sub1 : tensor<*xf32>
}

// Multiple non contiguous pass through values
// CHECK-LABEL: testWhileRegionMultiplePassThroughNonContiguous
func @testWhileRegionMultiplePassThroughNonContiguous(%arg0 : tensor<*xf32>, %arg1 : tensor<*xf32>, %arg2 : tensor<*xf32>, %arg3 : tensor<i32>) -> tensor<*xf32> {
  // Verify arg0 and arg2 are eliminated
  // CHECK: %[[WHILE_OUT:.*]]:2 = "tf.WhileRegion"(%arg1, %arg3)
  %0:4 = "tf.WhileRegion"(%arg0, %arg1, %arg2, %arg3) (
    {
      // condition, check if count has reached 0
      ^bb0(%carg0 : tensor<*xf32>, %carg1 : tensor<*xf32>, %carg2 : tensor<*xf32>, %carg3 : tensor<i32>):
      %zero = constant dense<0> : tensor<i32>
      %ne = "tf.NotEqual"(%carg3, %zero) : (tensor<i32>, tensor<i32>) -> tensor<i1>
      "tf.Yield"(%ne) : (tensor<i1>) -> ()
    },
    {
      // loop body
      ^bb0(%barg0 : tensor<*xf32>, %barg1 : tensor<*xf32>, %barg2 : tensor<*xf32>, %barg3 : tensor<i32>):
      %arg1neg = "tf.Neg"(%barg1) : (tensor<*xf32>) -> tensor<*xf32>
      %one = constant dense<1> : tensor<i32>
      %sub = "tf.Sub"(%barg3, %one) : (tensor<i32>, tensor<i32>) -> tensor<i32>
      "tf.Yield"(%barg0, %arg1neg, %barg2, %sub) : (tensor<*xf32>, tensor<*xf32>, tensor<*xf32>, tensor<i32>) -> ()
    }
  ) { is_stateless = false } : (tensor<*xf32>, tensor<*xf32>, tensor<*xf32>, tensor<i32>) -> (tensor<*xf32>, tensor<*xf32>, tensor<*xf32>, tensor<i32>)

  // Verify that use of while loop results corresponding to result #0 and 2 of
  // the while are replaces with corresponding WhileRegion operands
  // CHECK: %[[SUB0:.*]] = "tf.Sub"(%arg0, %[[WHILE_OUT]]#0)
  // CHECK: %[[SUB1:.*]] = "tf.Sub"(%arg2, %[[SUB0]])
  // CHECK: return %[[SUB1]]
  %sub0 = "tf.Sub" (%0#0, %0#1) : (tensor<*xf32>, tensor<*xf32>) -> tensor<*xf32>
  %sub1 = "tf.Sub" (%0#2, %sub0) : (tensor<*xf32>, tensor<*xf32>) -> tensor<*xf32>
  return %sub1 : tensor<*xf32>
}

// Pass through but with type mismatch (tensor<*xf32> is compatible with
// tensor<?x?xf32> in the body). WhileRegion canonicalization does not handle
// this.
// CHECK-LABEL: testWhileRegionPassThroughTypeMismatch
func @testWhileRegionPassThroughTypeMismatch(%arg0 : tensor<*xf32>, %arg1 : tensor<i32>) -> tensor<*xf32> {
  // Verify that the While stay's unchanged
  // CHECK: "tf.WhileRegion"(%arg0, %arg1)
  %0:2 = "tf.WhileRegion"(%arg0, %arg1) (
    {
      // condition, check if count has reached 0
      ^bb0(%carg0: tensor<*xf32>, %carg1: tensor<i32>):
      %zero = constant dense<0> : tensor<i32>
      %ne = "tf.NotEqual"(%carg1, %zero) : (tensor<i32>, tensor<i32>) -> tensor<i1>
      "tf.Yield"(%ne) : (tensor<i1>) -> ()
    },
    {
      // loop body
      ^bb0(%barg0: tensor<?x?xf32>, %barg1: tensor<i32>):
      %one = constant dense<1> : tensor<i32>
      %sub = "tf.Sub"(%barg1, %one) : (tensor<i32>, tensor<i32>) -> tensor<i32>
      "tf.Yield"(%barg0, %sub) : (tensor<?x?xf32>, tensor<i32>) -> ()
    }
  ) { is_stateless = false } : (tensor<*xf32>, tensor<i32>) -> (tensor<*xf32>, tensor<i32>)
  // Verify that the result stays uchanged
  // CHECK: return %arg0 : tensor<*xf32>
  return %0#0 : tensor<*xf32>
}

// Unused value flowing through the while (operand 2 and 3, is unused in the
// while and the corresponding result is unused as well). Canonicalization will
// eliminate them.
// CHECK-LABEL: testWhileRegionUnusedValue
func @testWhileRegionUnusedValue(%arg0 : tensor<*xf32>, %arg1 : tensor<i32>, %arg2: tensor<i32>) -> tensor<*xf32> {
  %cst = constant dense <33.0> : tensor<f32>
  // Verify that last 2 operands of while (unused) are removed
  // CHECK: %[[WHILE_OUT:.*]]:2 = "tf.WhileRegion"(%arg0, %arg1)
  %0:4 = "tf.WhileRegion"(%arg0, %arg1, %arg2, %cst) (
    {
      // condition, check if count has reached 0
      ^bb0(%carg0: tensor<*xf32>, %carg1: tensor<i32>, %carg2:tensor<i32>, %carg3:tensor<f32>):
      %zero = constant dense<0> : tensor<i32>
      %ne = "tf.NotEqual"(%carg1, %zero) : (tensor<i32>, tensor<i32>) -> tensor<i1>
      "tf.Yield"(%ne) : (tensor<i1>) -> ()
    },
    {
      // loop body
      ^bb0(%barg0: tensor<*xf32>, %barg1: tensor<i32>, %barg2:tensor<i32>, %barg3:tensor<f32>):
      %add = "tf.Add"(%barg0, %barg0) : (tensor<*xf32>, tensor<*xf32>) -> tensor<*xf32>
      %one = constant dense<1> : tensor<i32>
      %sub = "tf.Sub"(%barg1, %one) : (tensor<i32>, tensor<i32>) -> tensor<i32>
      %dummy0 = constant dense<7> : tensor<i32>
      %dummy1 = constant dense<3.0> : tensor<f32>
      "tf.Yield"(%add, %sub, %dummy0, %dummy1) : (tensor<*xf32>, tensor<i32>, tensor<i32>, tensor<f32>) -> ()
    }
  ) { is_stateless = false } : (tensor<*xf32>, tensor<i32>,  tensor<i32>, tensor<f32>) -> (tensor<*xf32>, tensor<i32>,  tensor<i32>, tensor<f32>)

  // Verify that return still uses while result # 0
  // CHECK: return %[[WHILE_OUT]]#0 : tensor<*xf32>
  return %0#0 : tensor<*xf32>
}

// Check that output_shapes attribute is removed for tf.If
func @testIfThen(tensor<*xf32>) -> tensor<*xf32>
func @testIfElse(tensor<*xf32>) -> tensor<*xf32>
// CHECK-LABEL: func @testIfDropOutputShapes
func @testIfDropOutputShapes(tensor<i1>, tensor<2xf32>) -> tensor<2xf32> {
^bb0(%arg0: tensor<i1>, %arg1: tensor<2xf32>):
  // CHECK: "tf.If"
  // CHECK-NOT: output_shapes
  %1 = "tf.If"(%arg0, %arg1) {
    then_branch = @testIfThen, else_branch = @testIfElse, is_stateless = false, output_shapes = [#tf.shape<>]
  } : (tensor<i1>, tensor<2xf32>) -> tensor<2xf32>

  return %1 : tensor<2xf32>
}

// Check that output_shapes attribute is removed for tf.Whileß
func @testWhileCond(tensor<*xf32>) -> (tensor<i1>)
func @testWhileBody(tensor<*xf32>) -> (tensor<*xf32>)
// CHECK-LABEL: func @testWhileDropOutputShapes
func @testWhileDropOutputShapes(tensor<*xf32>) -> (tensor<*xf32>) {
^bb0(%arg0: tensor<*xf32>):
  // CHECK: "tf.While"
  // CHECK-NOT: output_shapes
  %1 = "tf.While"(%arg0) {
    cond = @testWhileCond,
    body = @testWhileBody,
    is_stateless = false,
    output_shapes = [#tf.shape<>]
  } : (tensor<*xf32>) -> (tensor<*xf32>)

  return %1 : tensor<*xf32>
}

// CHECK-LABEL: testNMSV3ToNMSV4
func @testNMSV3ToNMSV4(%arg0: tensor<3x4xf32>, %arg1: tensor<3xf32>, %arg2: tensor<f32>, %arg3: tensor<f32>) -> tensor<2xi32> {
  %max_size = constant dense<2> : tensor<i32>
  // CHECK: "tf.NonMaxSuppressionV4"
  %0 = "tf.NonMaxSuppressionV3"(%arg0, %arg1, %max_size, %arg2, %arg3): (tensor<3x4xf32>, tensor<3xf32>, tensor<i32>, tensor<f32>, tensor<f32>) -> (tensor<2xi32>)
  return %0 : tensor<2xi32>
}

// CHECK-LABEL: testFusedBatchNormToBatchNormV3
func @testFusedBatchNormToBatchNormV3(%arg0: tensor<8x8x8x8xf32>, %arg1: tensor<8xf32>, %arg2: tensor<8xf32>, %arg3: tensor<8xf32>, %arg4: tensor<8xf32>) -> (tensor<8x8x8x8xf32>) {
  // CHECK: "tf.FusedBatchNormV3"
  %0:5 = "tf.FusedBatchNorm"(%arg0, %arg1, %arg2, %arg3, %arg4): (tensor<8x8x8x8xf32>, tensor<8xf32>, tensor<8xf32>, tensor<8xf32>, tensor<8xf32>) -> (tensor<8x8x8x8xf32>, tensor<8xf32>, tensor<8xf32>, tensor<8xf32>, tensor<8xf32> )
  return %0#0  : tensor<8x8x8x8xf32>
}
