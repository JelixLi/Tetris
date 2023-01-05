// RUN: mlir-hlo-opt %s -verify-diagnostics -split-input-file | mlir-hlo-opt | FileCheck %s

// -----

// CHECK-LABEL: func @ceil
func @ceil(%input: memref<2x2xf32>, %result: memref<2x2xf32>) {
  "lmhlo.ceil"(%input, %result) : (memref<2x2xf32>, memref<2x2xf32>) -> ()
  return
}

// -----

func @ceil(%input: memref<2x2xi32>, %result: memref<2x2xi32>) {
  // expected-error@+1{{must be memref of floating-point values}}
  "lmhlo.ceil"(%input, %result) : (memref<2x2xi32>, memref<2x2xi32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @cos
func @cos(%input: memref<2x2xf32>, %result: memref<2x2xf32>) {
  "lmhlo.cosine"(%input, %result) : (memref<2x2xf32>, memref<2x2xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @cos
func @cos(%input: memref<2x2xcomplex<f32>>, %result: memref<2x2xcomplex<f32>>) {
  "lmhlo.cosine"(%input, %result) : (memref<2x2xcomplex<f32>>, memref<2x2xcomplex<f32>>) -> ()
  return
}

// -----

func @cos(%input: memref<2x2xi32>, %result: memref<2x2xi32>) {
  // expected-error@+1{{must be memref of floating-point or complex-type values}}
  "lmhlo.cosine"(%input, %result) : (memref<2x2xi32>, memref<2x2xi32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @sin
func @sin(%input: memref<2x2xf32>, %result: memref<2x2xf32>) {
  "lmhlo.sine"(%input, %result) : (memref<2x2xf32>, memref<2x2xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @sin
func @sin(%input: memref<2x2xcomplex<f32>>, %result: memref<2x2xcomplex<f32>>) {
  "lmhlo.sine"(%input, %result) : (memref<2x2xcomplex<f32>>, memref<2x2xcomplex<f32>>) -> ()
  return
}

// -----

func @sin(%input: memref<2x2xi32>, %result: memref<2x2xi32>) {
  // expected-error@+1{{must be memref of floating-point or complex-type values}}
  "lmhlo.sine"(%input, %result) : (memref<2x2xi32>, memref<2x2xi32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @add_memrefs
func @add_memrefs(%arg0: memref<1xi32>, %arg1: memref<1xi32>, %arg_out: memref<1xi32>) -> () {
  "lmhlo.add"(%arg0, %arg1, %arg_out) : (memref<1xi32>, memref<1xi32>, memref<1xi32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @abs_memref
func @abs_memref(%in: memref<10xf32>, %out: memref<10xf32>) -> () {
  "lmhlo.abs"(%in, %out) : (memref<10xf32>, memref<10xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @convert_memref
func @convert_memref(%in: memref<10xf32>, %out: memref<10xi32>) -> () {
  "lmhlo.convert"(%in, %out) : (memref<10xf32>, memref<10xi32>) -> ()
  return
}

// -----

func @convert_memref(%in: memref<10xf32>, %out: memref<9xi32>) -> () {
  // expected-error@+1{{requires the same shape for all operands}}
  "lmhlo.convert"(%in, %out) : (memref<10xf32>, memref<9xi32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @exp
func @exp(%input: memref<2x2xf32>, %result: memref<2x2xf32>) {
  "lmhlo.exponential"(%input, %result) : (memref<2x2xf32>, memref<2x2xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @exp
func @exp(%input: memref<2x2xcomplex<f32>>, %result: memref<2x2xcomplex<f32>>) {
  "lmhlo.exponential"(%input, %result) : (memref<2x2xcomplex<f32>>, memref<2x2xcomplex<f32>>) -> ()
  return
}

// -----

func @exp(%input: memref<2x2xi32>, %result: memref<2x2xi32>) {
  // expected-error@+1{{must be memref of floating-point or complex-type values}}
  "lmhlo.exponential"(%input, %result) : (memref<2x2xi32>, memref<2x2xi32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @log_memref
func @log_memref(%in: memref<10xf32>, %out: memref<10xf32>) -> () {
  "lmhlo.log"(%in, %out) : (memref<10xf32>, memref<10xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @log_memref
func @log_memref(%in: memref<10xcomplex<f32>>, %out: memref<10xcomplex<f32>>) -> () {
  "lmhlo.log"(%in, %out) : (memref<10xcomplex<f32>>, memref<10xcomplex<f32>>) -> ()
  return
}

// -----

func @log_memref(%in: memref<10xi32>, %out: memref<10xi32>) -> () {
  // expected-error@+1{{must be memref of floating-point or complex-type values}}
  "lmhlo.log"(%in, %out) : (memref<10xi32>, memref<10xi32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @neg_memref
func @neg_memref(%in: memref<10xf32>, %out: memref<10xf32>) -> () {
  "lmhlo.negate"(%in, %out) : (memref<10xf32>, memref<10xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @rsqrt_memref
func @rsqrt_memref(%in: memref<10xf32>, %out: memref<10xf32>) -> () {
  "lmhlo.rsqrt"(%in, %out) : (memref<10xf32>, memref<10xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @rsqrt_memref
func @rsqrt_memref(%in: memref<10xcomplex<f32>>, %out: memref<10xcomplex<f32>>) -> () {
  "lmhlo.rsqrt"(%in, %out) : (memref<10xcomplex<f32>>, memref<10xcomplex<f32>>) -> ()
  return
}

// -----

func @rsqrt_memref(%in: memref<10xi32>, %out: memref<10xi32>) -> () {
  // expected-error@+1{{must be memref of floating-point or complex-type values}}
  "lmhlo.rsqrt"(%in, %out) : (memref<10xi32>, memref<10xi32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @sqrt_memref
func @sqrt_memref(%in: memref<10xf32>, %out: memref<10xf32>) -> () {
  "lmhlo.sqrt"(%in, %out) : (memref<10xf32>, memref<10xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @sqrt_memref
func @sqrt_memref(%in: memref<10xcomplex<f32>>, %out: memref<10xcomplex<f32>>) -> () {
  "lmhlo.sqrt"(%in, %out) : (memref<10xcomplex<f32>>, memref<10xcomplex<f32>>) -> ()
  return
}

// -----

func @sqrt_memref(%in: memref<10xi32>, %out: memref<10xi32>) -> () {
  // expected-error@+1{{must be memref of floating-point or complex-type values}}
  "lmhlo.sqrt"(%in, %out) : (memref<10xi32>, memref<10xi32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @sign_memref
func @sign_memref(%in: memref<10xf32>, %out: memref<10xf32>) -> () {
  "lmhlo.sign"(%in, %out) : (memref<10xf32>, memref<10xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @tanh_memref
func @tanh_memref(%in: memref<10xf32>, %out: memref<10xf32>) -> () {
  "lmhlo.tanh"(%in, %out) : (memref<10xf32>, memref<10xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @tanh_memref
func @tanh_memref(%in: memref<10xcomplex<f32>>, %out: memref<10xcomplex<f32>>) -> () {
  "lmhlo.tanh"(%in, %out) : (memref<10xcomplex<f32>>, memref<10xcomplex<f32>>) -> ()
  return
}

// -----

func @tanh_memref(%in: memref<10xi32>, %out: memref<10xi32>) -> () {
  // expected-error@+1{{must be memref of floating-point or complex-type values}}
  "lmhlo.tanh"(%in, %out) : (memref<10xi32>, memref<10xi32>) -> ()
  return
}

// -----

func @tanh_memref(%arg0: memref<1xf32>, %arg1: memref<2xf32>) -> () {
  // expected-error@+1{{'lmhlo.tanh' op requires all operands to have the same type}}
  "lmhlo.tanh"(%arg0, %arg1) : (memref<1xf32>, memref<2xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @add_memref
func @add_memref(%lhs: memref<10xf32>, %rhs: memref<10xf32>, %out: memref<10xf32>) -> () {
  "lmhlo.add"(%lhs, %rhs, %out) : (memref<10xf32>, memref<10xf32>, memref<10xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @div_memref
func @div_memref(%lhs: memref<10xf32>, %rhs: memref<10xf32>, %out: memref<10xf32>) -> () {
  "lmhlo.divide"(%lhs, %rhs, %out) : (memref<10xf32>, memref<10xf32>, memref<10xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @max_memref
func @max_memref(%lhs: memref<10xf32>, %rhs: memref<10xf32>, %out: memref<10xf32>) -> () {
  "lmhlo.maximum"(%lhs, %rhs, %out) : (memref<10xf32>, memref<10xf32>, memref<10xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @min_memref
func @min_memref(%lhs: memref<10xf32>, %rhs: memref<10xf32>, %out: memref<10xf32>) -> () {
  "lmhlo.minimum"(%lhs, %rhs, %out) : (memref<10xf32>, memref<10xf32>, memref<10xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @mul_memref
func @mul_memref(%lhs: memref<10xf32>, %rhs: memref<10xf32>, %out: memref<10xf32>) -> () {
  "lmhlo.multiply"(%lhs, %rhs, %out) : (memref<10xf32>, memref<10xf32>, memref<10xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @sub_memref
func @sub_memref(%lhs: memref<10xf32>, %rhs: memref<10xf32>, %out: memref<10xf32>) -> () {
  "lmhlo.subtract"(%lhs, %rhs, %out) : (memref<10xf32>, memref<10xf32>, memref<10xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @and_memref
func @and_memref(%lhs: memref<10xi32>, %rhs: memref<10xi32>, %out: memref<10xi32>) -> () {
  "lmhlo.and"(%lhs, %rhs, %out) : (memref<10xi32>, memref<10xi32>, memref<10xi32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @and_memref
func @and_memref(%lhs: memref<10xi1>, %rhs: memref<10xi1>, %out: memref<10xi1>) -> () {
  "lmhlo.and"(%lhs, %rhs, %out) : (memref<10xi1>, memref<10xi1>, memref<10xi1>) -> ()
  return
}

// -----

func @and_memref(%lhs: memref<10xf32>, %rhs: memref<10xf32>, %out: memref<10xf32>) -> () {
  // expected-error @+1 {{must be memref of 8/16/32/64-bit signless integer or 8/16/32/64-bit unsigned integer or pred (AKA boolean or 1-bit integer) values}}
  "lmhlo.and"(%lhs, %rhs, %out) : (memref<10xf32>, memref<10xf32>, memref<10xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @or_memref
func @or_memref(%lhs: memref<10xi32>, %rhs: memref<10xi32>, %out: memref<10xi32>) -> () {
  "lmhlo.or"(%lhs, %rhs, %out) : (memref<10xi32>, memref<10xi32>, memref<10xi32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @or_memref
func @or_memref(%lhs: memref<10xi1>, %rhs: memref<10xi1>, %out: memref<10xi1>) -> () {
  "lmhlo.or"(%lhs, %rhs, %out) : (memref<10xi1>, memref<10xi1>, memref<10xi1>) -> ()
  return
}

// -----

func @or_memref(%lhs: memref<10xf32>, %rhs: memref<10xf32>, %out: memref<10xf32>) -> () {
  // expected-error @+1 {{must be memref of 8/16/32/64-bit signless integer or 8/16/32/64-bit unsigned integer or pred (AKA boolean or 1-bit integer) values}}
  "lmhlo.or"(%lhs, %rhs, %out) : (memref<10xf32>, memref<10xf32>, memref<10xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @xor_memref
func @xor_memref(%lhs: memref<10xi32>, %rhs: memref<10xi32>, %out: memref<10xi32>) -> () {
  "lmhlo.xor"(%lhs, %rhs, %out) : (memref<10xi32>, memref<10xi32>, memref<10xi32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @xor_memref
func @xor_memref(%lhs: memref<10xi1>, %rhs: memref<10xi1>, %out: memref<10xi1>) -> () {
  "lmhlo.xor"(%lhs, %rhs, %out) : (memref<10xi1>, memref<10xi1>, memref<10xi1>) -> ()
  return
}

// -----

func @xor_memref(%lhs: memref<10xf32>, %rhs: memref<10xf32>, %out: memref<10xf32>) -> () {
  // expected-error @+1 {{must be memref of 8/16/32/64-bit signless integer or 8/16/32/64-bit unsigned integer or pred (AKA boolean or 1-bit integer) values}}
  "lmhlo.xor"(%lhs, %rhs, %out) : (memref<10xf32>, memref<10xf32>, memref<10xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @broadcast_in_dim_memref
func @broadcast_in_dim_memref(%arg0: memref<1x2xi32>, %out: memref<1x2x2xi32>) -> () {
  "lmhlo.broadcast_in_dim"(%arg0, %out) {broadcast_dimensions = dense<[1, 2]> : tensor<2xi64>} : (memref<1x2xi32>, memref<1x2x2xi32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @broadcast_in_dim_zero_rank_memref
func @broadcast_in_dim_zero_rank_memref(%arg0: memref<i32>, %out: memref<1x2x3xi32>) -> () {
  "lmhlo.broadcast_in_dim"(%arg0, %out) {broadcast_dimensions = dense<[]> : tensor<0xi64>} : (memref<i32>, memref<1x2x3xi32>) -> ()
  return
}

// -----


// CHECK-LABEL: func @reduce_memref
func @reduce_memref(%input: memref<10xf32>, %init: memref<f32>, %out: memref<1xf32>) -> () {
  "lmhlo.reduce"(%input, %init, %out) ( {
  ^bb0(%arg1: memref<f32>, %arg2: memref<f32>, %result: memref<f32>):
    "lmhlo.add"(%arg1, %arg2, %result) : (memref<f32>, memref<f32>, memref<f32>) -> ()
    "lmhlo.terminator"() : () -> ()
  } ) {dimensions = dense<[0]> : tensor<1xi64>} : (memref<10xf32>, memref<f32>, memref<1xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @fusion_memref
func @fusion_memref(%input1: memref<10xf32>, %input2: memref<10xf32>, %input3: memref<10xf32>, %out: memref<10xf32>) -> () {
  "lmhlo.fusion"() ( {
    %0 = tensor_load %input1 : memref<10xf32>
    %1 = tensor_load %input2 : memref<10xf32>
    %2 = "mhlo.add"(%0, %1) {name = "add"} : (tensor<10xf32>, tensor<10xf32>) -> tensor<10xf32>
    %3 = tensor_load %input3 : memref<10xf32>
    %4 = "mhlo.multiply"(%2, %3) {name = "multiply"} : (tensor<10xf32>, tensor<10xf32>) -> tensor<10xf32>
    tensor_store %4, %out : memref<10xf32>
    "lmhlo.terminator"() : () -> ()
  } ) : () -> ()
  return
}

// -----

// CHECK-LABEL: func @case_memref
func @case_memref(%index: memref<i32>, %operand_1: memref<f32>, %operand_2: memref<f32>, %operand_3: memref<f32>, %out: memref<f32>) -> () {
  "lmhlo.case"(%index, %operand_1, %operand_2, %operand_3, %out) ( {
    ^bb0(%arg0: memref<f32>):
      "lmhlo.negate"(%arg0, %out) : (memref<f32>, memref<f32>) -> ()
      "lmhlo.terminator"() : () -> ()
    },  {
    ^bb0(%arg0: memref<f32>):
      "lmhlo.copy"(%arg0, %out) : (memref<f32>, memref<f32>) -> ()
      "lmhlo.terminator"() : () -> ()
    },  {
    ^bb0(%arg0: memref<f32>):
      "lmhlo.add"(%arg0, %arg0, %out) : (memref<f32>, memref<f32>, memref<f32>) -> ()
      "lmhlo.terminator"() : () -> ()
    }
  ) {operand_segment_sizes = dense<[1, 3, 1]> : vector<3xi32>}
  : (memref<i32>, memref<f32>, memref<f32>, memref<f32>, memref<f32>) -> ()
  return
}

// -----

func @static_memref_cast(%in: memref<10x1xf32>) {
  %out = lmhlo.static_memref_cast %in
           : memref<10x1xf32> -> memref<10xf32, offset: 0, strides: [1]>
  return
}
// CHECK-LABEL: func @static_memref_cast

// -----

func @static_memref_cast_dynamic_operand(%in: memref<10x?xf32>) {
  // expected-error @+1 {{operand must have static shape}}
  %out = lmhlo.static_memref_cast %in
           : memref<10x?xf32> -> memref<10x1xf32, offset: 0, strides: [10, 1]>
  return
}

// -----

func @static_memref_cast_dynamic_result(%in: memref<10x1xf32>) {
  // expected-error @+1 {{result must have static shape}}
  %out = lmhlo.static_memref_cast %in
           : memref<10x1xf32> -> memref<10x?xf32, offset: 0, strides: [?, ?]>
  return
}

// -----

func @dynamic_memref_cast(%in: memref<?xf32>) {
  %size = constant 10 : index
  %step = constant 1 : index
  %out = lmhlo.dynamic_memref_cast %in(%size)[%step]
           : memref<?xf32> -> memref<?xf32, offset: 0, strides: [?]>
  return
}
// CHECK-LABEL: func @dynamic_memref_cast

// -----

func @dynamic_memref_cast_incompatible_result_type(%in: memref<?xf32>) {
  // expected-error @+3 {{`sizes` args count must be equal to the rank of the output memref}}
  %size = constant 10 : index
  %step = constant 1 : index
  %out = lmhlo.dynamic_memref_cast %in(%size)[%step]
           : memref<?xf32> -> memref<?x?xf32, offset: 0, strides: [?, ?]>
  return
}
// -----

// CHECK-LABEL: func @reshape_memref_cast(
func @reshape_memref_cast(%unranked: memref<*xf32>, %shape1: memref<1xi32>,
         %shape2: memref<2xi32>, %shape3: memref<?xi32>) {
  // CHECK-SAME: [[UNRANKED:%.*]]: memref<*xf32>, [[SHAPE_1:%.*]]: memref<1xi32>,
  // CHECK-SAME: [[SHAPE_2:%.*]]: memref<2xi32>, [[SHAPE_3:%.*]]: memref<?xi32>

  // CHECK-NEXT: [[DYN_VEC:%.*]] = lmhlo.reshape_memref_cast [[UNRANKED]]
  // CHECK-SAME:     : (memref<*xf32>, memref<1xi32>) -> memref<?xf32>
  %dyn_vec = lmhlo.reshape_memref_cast %unranked(%shape1)
               : (memref<*xf32>, memref<1xi32>) -> memref<?xf32>

  // CHECK-NEXT: [[DYN_MAT:%.*]] = lmhlo.reshape_memref_cast [[DYN_VEC]]
  // CHECK-SAME:     : (memref<?xf32>, memref<2xi32>) -> memref<?x?xf32>
  %dyn_mat = lmhlo.reshape_memref_cast %dyn_vec(%shape2)
               : (memref<?xf32>, memref<2xi32>) -> memref<?x?xf32>

  // CHECK-NEXT: {{%.*}} = lmhlo.reshape_memref_cast [[DYN_MAT]]
  // CHECK-SAME:     : (memref<?x?xf32>, memref<?xi32>) -> memref<*xf32>
  %new_unranked = lmhlo.reshape_memref_cast %dyn_mat(%shape3)
               : (memref<?x?xf32>, memref<?xi32>) -> memref<*xf32>
  return
}

// -----

func @reshape_memref_cast_element_type_mismatch(
       %buf: memref<*xf32>, %shape: memref<1xi32>) {
  // expected-error @+1 {{element types of source and destination memref types should be the same}}
  lmhlo.reshape_memref_cast %buf(%shape)
    : (memref<*xf32>, memref<1xi32>) -> memref<?xi32>
}

// -----

func @reshape_memref_cast_dst_ranked_shape_unranked(
       %buf: memref<*xf32>, %shape: memref<?xi32>) {
  // expected-error @+1 {{cannot use shape operand with dynamic length to cast statically-ranked memref type}}
  lmhlo.reshape_memref_cast %buf(%shape)
    : (memref<*xf32>, memref<?xi32>) -> memref<?xf32>
  return
}

// -----

func @reshape_memref_cast_dst_shape_rank_mismatch(
       %buf: memref<*xf32>, %shape: memref<1xi32>) {
  // expected-error @+1 {{length of shape operand differs from the result's memref rank}}
  lmhlo.reshape_memref_cast %buf(%shape)
    : (memref<*xf32>, memref<1xi32>) -> memref<?x?xf32>
  return
}

// -----

func @reshape_memref_cast_affine_map_is_not_identity(
        %buf: memref<4x4xf32, offset: 0, strides: [3, 2]>,
        %shape: memref<1xi32>) {
  // expected-error @+1 {{operand memref type should have identity affine map}}
  lmhlo.reshape_memref_cast %buf(%shape)
    : (memref<4x4xf32, offset: 0, strides: [3, 2]>, memref<1xi32>)
    -> memref<8xf32>
  return
}

// -----

// CHECK-LABEL: func @atan2_memrefs
func @atan2_memrefs(%arg0: memref<1xf32>, %arg1: memref<1xf32>, %arg_out: memref<1xf32>) -> () {
  "lmhlo.atan2"(%arg0, %arg1, %arg_out) : (memref<1xf32>, memref<1xf32>, memref<1xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @atan2_memrefs
func @atan2_memrefs(%arg0: memref<1xcomplex<f32>>, %arg1: memref<1xcomplex<f32>>, %arg_out: memref<1xcomplex<f32>>) -> () {
  "lmhlo.atan2"(%arg0, %arg1, %arg_out) : (memref<1xcomplex<f32>>, memref<1xcomplex<f32>>, memref<1xcomplex<f32>>) -> ()
  return
}

// -----

func @atan2_memrefs(%arg0: memref<1xi32>, %arg1: memref<1xi32>, %arg_out: memref<1xi32>) -> () {
  // expected-error@+1{{must be memref of floating-point or complex-type values}}
  "lmhlo.atan2"(%arg0, %arg1, %arg_out) : (memref<1xi32>, memref<1xi32>, memref<1xi32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @bitcast_convert_memrefs
func @bitcast_convert_memrefs(%arg0: memref<1xf32>, %arg_out: memref<1xi32>) -> () {
  "lmhlo.bitcast_convert"(%arg0, %arg_out) : (memref<1xf32>, memref<1xi32>) -> ()
  return
}

// -----

func @bitcast_convert_memrefs(%arg0: memref<1xf32>, %arg_out: memref<2xi32>) -> () {
  // expected-error@+1{{requires the same shape for all operands}}
  "lmhlo.bitcast_convert"(%arg0, %arg_out) : (memref<1xf32>, memref<2xi32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @clz_memrefs
func @clz_memrefs(%arg0: memref<1xi32>, %arg_out: memref<1xi32>) -> () {
  "lmhlo.count_leading_zeros"(%arg0, %arg_out) : (memref<1xi32>, memref<1xi32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @expm1_memrefs
func @expm1_memrefs(%arg0: memref<1xf32>, %arg_out: memref<1xf32>) -> () {
  "lmhlo.exponential_minus_one"(%arg0, %arg_out) : (memref<1xf32>, memref<1xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @expm1_memrefs
func @expm1_memrefs(%arg0: memref<1xcomplex<f32>>, %arg_out: memref<1xcomplex<f32>>) -> () {
  "lmhlo.exponential_minus_one"(%arg0, %arg_out) : (memref<1xcomplex<f32>>, memref<1xcomplex<f32>>) -> ()
  return
}

// -----

// CHECK-LABEL: func @floor_memrefs
func @floor_memrefs(%arg0: memref<1xf32>, %arg_out: memref<1xf32>) -> () {
  "lmhlo.floor"(%arg0, %arg_out) : (memref<1xf32>, memref<1xf32>) -> ()
  return
}

// -----

func @floor_memrefs(%arg0: memref<1xi32>, %arg_out: memref<1xi32>) -> () {
  // expected-error@+1{{must be memref of floating-point values}}
  "lmhlo.floor"(%arg0, %arg_out) : (memref<1xi32>, memref<1xi32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @imag_memrefs
func @imag_memrefs(%arg0: memref<1xcomplex<f32>>, %arg_out: memref<1xf32>) -> () {
  "lmhlo.imag"(%arg0, %arg_out) : (memref<1xcomplex<f32>>, memref<1xf32>) -> ()
  return
}

// -----

func @imag_memrefs(%arg0: memref<1xf32>, %arg_out: memref<1xf32>) -> () {
  // expected-error@+1{{must be memref of complex-type values}}
  "lmhlo.imag"(%arg0, %arg_out) : (memref<1xf32>, memref<1xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @real_memrefs
func @real_memrefs(%arg0: memref<1xcomplex<f32>>, %arg_out: memref<1xf32>) -> () {
  "lmhlo.real"(%arg0, %arg_out) : (memref<1xcomplex<f32>>, memref<1xf32>) -> ()
  return
}

// -----

func @real_memrefs(%arg0: memref<1xf32>, %arg_out: memref<1xf32>) -> () {
  // expected-error@+1{{must be memref of complex-type values}}
  "lmhlo.real"(%arg0, %arg_out) : (memref<1xf32>, memref<1xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @is_finite_memrefs
func @is_finite_memrefs(%arg0: memref<1xf32>, %arg_out: memref<1xi1>) -> () {
  "lmhlo.is_finite"(%arg0, %arg_out) : (memref<1xf32>, memref<1xi1>) -> ()
  return
}

// -----

// CHECK-LABEL: func @log1p_memrefs
func @log1p_memrefs(%arg0: memref<1xf32>, %arg_out: memref<1xf32>) -> () {
  "lmhlo.log_plus_one"(%arg0, %arg_out) : (memref<1xf32>, memref<1xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @log1p_memrefs
func @log1p_memrefs(%arg0: memref<1xcomplex<f32>>, %arg_out: memref<1xcomplex<f32>>) -> () {
  "lmhlo.log_plus_one"(%arg0, %arg_out) : (memref<1xcomplex<f32>>, memref<1xcomplex<f32>>) -> ()
  return
}

// -----

func @log1p_memref(%in: memref<10xi32>, %out: memref<10xi32>) -> () {
  // expected-error@+1{{must be memref of floating-point or complex-type values}}
  "lmhlo.log_plus_one"(%in, %out) : (memref<10xi32>, memref<10xi32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @not_memrefs
func @not_memrefs(%arg0: memref<1xi32>, %arg_out: memref<1xi32>) -> () {
  "lmhlo.not"(%arg0, %arg_out) : (memref<1xi32>, memref<1xi32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @not_memrefs
func @not_memrefs(%arg0: memref<1xi1>, %arg_out: memref<1xi1>) -> () {
  "lmhlo.not"(%arg0, %arg_out) : (memref<1xi1>, memref<1xi1>) -> ()
  return
}

// -----

func @not_memrefs(%arg0: memref<1xf32>, %arg_out: memref<1xf32>) -> () {
  // expected-error @+1 {{must be memref of 8/16/32/64-bit signless integer or 8/16/32/64-bit unsigned integer or pred (AKA boolean or 1-bit integer) values}}
  "lmhlo.not"(%arg0, %arg_out) : (memref<1xf32>, memref<1xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @popcnt_memrefs
func @popcnt_memrefs(%arg0: memref<1xi32>, %arg_out: memref<1xi32>) -> () {
  "lmhlo.popcnt"(%arg0, %arg_out) : (memref<1xi32>, memref<1xi32>) -> ()
  return
}

// -----

func @popcnt_memrefs(%arg0: memref<1xf32>, %arg_out: memref<1xf32>) -> () {
  // expected-error @+1 {{must be memref of 8/16/32/64-bit signless integer or 8/16/32/64-bit unsigned integer values}}
  "lmhlo.popcnt"(%arg0, %arg_out) : (memref<1xf32>, memref<1xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @reduce_precision_memrefs
func @reduce_precision_memrefs(%arg0: memref<1xf32>, %arg_out: memref<1xf32>) -> () {
  "lmhlo.reduce_precision"(%arg0, %arg_out) { exponent_bits = 4 : i32, mantissa_bits = 4 : i32 } : (memref<1xf32>, memref<1xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @round_memrefs
func @round_memrefs(%arg0: memref<1xf32>, %arg_out: memref<1xf32>) -> () {
  "lmhlo.round_nearest_afz"(%arg0, %arg_out) : (memref<1xf32>, memref<1xf32>) -> ()
  return
}

// -----

func @round_memrefs(%arg0: memref<1xi32>, %arg_out: memref<1xi32>) -> () {
  // expected-error@+1{{must be memref of floating-point values}}
  "lmhlo.round_nearest_afz"(%arg0, %arg_out) : (memref<1xi32>, memref<1xi32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @shift_left_memrefs
func @shift_left_memrefs(%arg0: memref<1xi32>, %arg1: memref<1xi32>, %arg_out: memref<1xi32>) -> () {
  "lmhlo.shift_left"(%arg0, %arg1, %arg_out) : (memref<1xi32>, memref<1xi32>, memref<1xi32>) -> ()
  return
}

// -----

func @shift_left_memrefs(%arg0: memref<1xf32>, %arg1: memref<1xf32>, %arg_out: memref<1xf32>) -> () {
  // expected-error @+1 {{must be memref of 8/16/32/64-bit signless integer or 8/16/32/64-bit unsigned integer values}}
  "lmhlo.shift_left"(%arg0, %arg1, %arg_out) : (memref<1xf32>, memref<1xf32>, memref<1xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @shift_right_arithmetic_memrefs
func @shift_right_arithmetic_memrefs(%arg0: memref<1xi32>, %arg1: memref<1xi32>, %arg_out: memref<1xi32>) -> () {
  "lmhlo.shift_right_arithmetic"(%arg0, %arg1, %arg_out) : (memref<1xi32>, memref<1xi32>, memref<1xi32>) -> ()
  return
}

// -----

func @shift_right_arithmetic_memrefs(%arg0: memref<1xf32>, %arg1: memref<1xf32>, %arg_out: memref<1xf32>) -> () {
  // expected-error @+1 {{must be memref of 8/16/32/64-bit signless integer or 8/16/32/64-bit unsigned integer values}}
  "lmhlo.shift_right_arithmetic"(%arg0, %arg1, %arg_out) : (memref<1xf32>, memref<1xf32>, memref<1xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @shift_right_logical_memrefs
func @shift_right_logical_memrefs(%arg0: memref<1xi32>, %arg1: memref<1xi32>, %arg_out: memref<1xi32>) -> () {
  "lmhlo.shift_right_logical"(%arg0, %arg1, %arg_out) : (memref<1xi32>, memref<1xi32>, memref<1xi32>) -> ()
  return
}

// -----

func @shift_right_logical_memrefs(%arg0: memref<1xf32>, %arg1: memref<1xf32>, %arg_out: memref<1xf32>) -> () {
  // expected-error @+1 {{must be memref of 8/16/32/64-bit signless integer or 8/16/32/64-bit unsigned integer values}}
  "lmhlo.shift_right_logical"(%arg0, %arg1, %arg_out) : (memref<1xf32>, memref<1xf32>, memref<1xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @all_reduce_memrefs
func @all_reduce_memrefs(%arg0: memref<10xf32>, %arg_out: memref<10xf32>) -> () {
  "lmhlo.all_reduce"(%arg0, %arg_out) ({
    ^bb0(%lhs: tensor<f32>, %rhs: tensor<f32>):
    %max = mhlo.maximum %lhs, %rhs : tensor<f32>
    "mhlo.return"(%max) : (tensor<f32>) -> ()
  })
  { replica_groups = dense<[[0, 2, 4, 6], [1, 3, 5, 7]]> : tensor<2x4xi64> }: (memref<10xf32>, memref<10xf32>) -> ()

  "lmhlo.all_reduce"(%arg0, %arg_out) ({
    ^bb0(%lhs: tensor<f32>, %rhs: tensor<f32>):
    %max = mhlo.maximum %lhs, %rhs : tensor<f32>
    "mhlo.return"(%max) : (tensor<f32>) -> ()
  })
  {
    replica_groups = dense<[[0, 2, 4, 6], [1, 3, 5, 7]]> : tensor<2x4xi64>,
    channel_id = { handle = 5 : i64, type = 2 : i64 },
    constrain_layout = true,
    use_global_device_ids = true
  }: (memref<10xf32>, memref<10xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @collective_permute_memrefs
func @collective_permute_memrefs(%arg0: memref<128x32xf32>, %arg_out: memref<128x32xf32>) -> () {
  "lmhlo.collective_permute"(%arg0, %arg_out) {
    source_target_pairs = dense<[[0, 1], [1, 2], [2, 3]]> : tensor<3x2xi64>
  } : (memref<128x32xf32>, memref<128x32xf32>) -> ()

  "lmhlo.collective_permute"(%arg0, %arg_out) {
    source_target_pairs = dense<[[0, 1], [1, 2], [2, 3]]> : tensor<3x2xi64>,
    channel_id = { handle = 5 : i64, type = 2 : i64 }
  } : (memref<128x32xf32>, memref<128x32xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @fft_memrefs
func @fft_memrefs(%arg0: memref<3x9xf32>, %arg_out: memref<3x5xcomplex<f32>>) -> () {
  "lmhlo.fft"(%arg0, %arg_out) {fft_length = dense<9> : tensor<1xi64>, fft_type = "RFFT"} : (memref<3x9xf32>, memref<3x5xcomplex<f32>>) -> ()
  return
}

// -----

// CHECK-LABEL: func @batch_norm_grad_memrefs
func @batch_norm_grad_memrefs(%arg0: memref<8x8x8x8xf32>, %arg1: memref<8xf32>, %arg2: memref<8xf32>,
                              %arg3: memref<8xf32>, %arg4: memref<8x8x8x8xf32>,
                              %grad_operand: memref<8x8x8x8xf32>, %grad_scale: memref<8xf32>,
                              %grad_offset: memref<8xf32>) -> () {
  "lmhlo.batch_norm_grad"(%arg0, %arg1, %arg2, %arg3, %arg4, %grad_operand, %grad_scale, %grad_offset) {epsilon = 1.000000e-03 : f32, feature_index = 3 : i64}
      : (memref<8x8x8x8xf32>, memref<8xf32>, memref<8xf32>, memref<8xf32>, memref<8x8x8x8xf32>,
         memref<8x8x8x8xf32>, memref<8xf32>, memref<8xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @batch_norm_inference_memrefs
func @batch_norm_inference_memrefs(%arg0: memref<8x8x8x8xf32>, %arg1: memref<8xf32>, %arg2: memref<8xf32>,
                                   %arg3: memref<8xf32>, %arg4: memref<8xf32>, %arg_out: memref<8x8x8x8xf32>) -> () {
  "lmhlo.batch_norm_inference"(%arg0, %arg1, %arg2, %arg3, %arg4, %arg_out) {epsilon = 1.000000e-03 : f32, feature_index = 3 : i64}
      : (memref<8x8x8x8xf32>, memref<8xf32>, memref<8xf32>, memref<8xf32>, memref<8xf32>, memref<8x8x8x8xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @batch_norm_training_memrefs
func @batch_norm_training_memrefs(%arg0: memref<8x8x8x8xf32>, %arg1: memref<8xf32>, %arg2: memref<8xf32>,
                                  %output: memref<8x8x8x8xf32>, %batch_mean: memref<8xf32>,
                                  %batch_var: memref<8xf32>) -> () {
  "lmhlo.batch_norm_training"(%arg0, %arg1, %arg2, %output, %batch_mean, %batch_var) {epsilon = 1.000000e-03 : f32, feature_index = 3 : i64}
      : (memref<8x8x8x8xf32>, memref<8xf32>, memref<8xf32>, memref<8x8x8x8xf32>, memref<8xf32>, memref<8xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @cholesky_memrefs
func @cholesky_memrefs(%arg0: memref<1x291x291xf32>, %arg_out: memref<1x291x291xf32>) -> () {
  "lmhlo.cholesky"(%arg0, %arg_out) : (memref<1x291x291xf32>, memref<1x291x291xf32>) -> ()
  "lmhlo.cholesky"(%arg0, %arg_out) { lower = true } : (memref<1x291x291xf32>, memref<1x291x291xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @infeed_memrefs
func @infeed_memrefs(%arg_out: memref<3xf32>) -> () {
  "lmhlo.infeed"(%arg_out) { config = "x" } : (memref<3xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @outfeed_memrefs
func @outfeed_memrefs(%arg0: memref<3xf32>) -> () {
  "lmhlo.outfeed"(%arg0) { config = "x" } : (memref<3xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @replica_id_memrefs
func @replica_id_memrefs(%arg_out: memref<ui32>) -> () {
  "lmhlo.replica_id"(%arg_out) : (memref<ui32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @triangular_solve_memrefs
func @triangular_solve_memrefs(%arg0: memref<4x4xf32>, %arg1: memref<3x4xf32>, %arg_out: memref<3x4xf32>) -> () {
  "lmhlo.triangular_solve"(%arg0, %arg1, %arg_out) {left_side = true, lower = true, transpose_a = "NO_TRANSPOSE", unit_diagonal = true}
      : (memref<4x4xf32>, memref<3x4xf32>, memref<3x4xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @while_memrefs
func @while_memrefs(%arg0: memref<i64>, %arg_out: memref<i64>) -> () {
  "lmhlo.while"(%arg0, %arg_out) (
    { ^bb0(%arg: memref<i64>, %cond: memref<i1>): "lmhlo.terminator"() : () -> () },
    { ^bb0(%arg: memref<i64>, %body_out: memref<i64>): "lmhlo.terminator"() : () -> () }
  ) : (memref<i64>, memref<i64>) -> ()
  return
}

// -----

// CHECK-LABEL: func @while_memrefs
func @while_memrefs(%arg0: memref<i64>, %arg1: memref<5xf32>, %arg0_out: memref<i64>, %arg1_out: memref<5xf32>) -> () {
  "lmhlo.while"(%arg0, %arg1, %arg0_out, %arg1_out) (
    { ^bb0(%cur0: memref<i64>, %cur1: memref<5xf32>, %cond: memref<i1>): "lmhlo.terminator"() : () -> () },
    { ^bb0(%cur0: memref<i64>, %cur1: memref<5xf32>, %body_out0: memref<i64>, %body_out1: memref<5xf32>): "lmhlo.terminator"() : () -> () }
  ) : (memref<i64>, memref<5xf32>, memref<i64>, memref<5xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @bitcast_memrefs
func @bitcast_memrefs(%arg0: memref<1xf64>, %arg_out: memref<2xi32>) -> () {
  "lmhlo.bitcast"(%arg0, %arg_out) : (memref<1xf64>, memref<2xi32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @scatter_memrefs
func @scatter_memrefs(%input: memref<200x100x300xf32>, %indices: memref<10x2xi32>,
                      %updates: memref<10x300xf32>, %arg_out: memref<200x100x300xf32>) -> () {
  "lmhlo.scatter" (%input, %indices, %updates, %arg_out) ({
  ^bb0(%lhs: tensor<f32>, %rhs: tensor<f32>): // no predecessors
    %add = mhlo.add %lhs, %rhs : tensor<f32>
    "mhlo.return"(%add) : (tensor<f32>) -> ()
  }) {
    scatter_dimension_numbers = {
      update_window_dims = dense<[1]> : tensor<1xi64>,
      inserted_window_dims = dense<[0, 1]> : tensor<2xi64>,
      scatter_dims_to_operand_dims = dense<[0, 1]> : tensor<2xi64>,
      index_vector_dim = 1 : i64
    },
    indices_are_sorted = true,
    unique_indices = true
  } : (memref<200x100x300xf32>, memref<10x2xi32>, memref<10x300xf32>, memref<200x100x300xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @map_memrefs
func @map_memrefs(%arg0: memref<20xf32>, %arg1: memref<20xf32>, %arg_out: memref<20xf32>) -> () {
  "lmhlo.map"(%arg0, %arg1, %arg_out) ({
    ^bb0(%a: tensor<f32>, %b: tensor<f32>):
    %c = mhlo.add %a, %b : tensor<f32>
    "mhlo.return"(%c) : (tensor<f32>) -> ()
  }) {dimensions = dense<0> : tensor<1xi64>} : (memref<20xf32>, memref<20xf32>, memref<20xf32>) -> ()
  return
}

// -----

func @map_memrefs(%arg0: memref<20xf32>, %arg1: memref<20xf32>, %arg_out: memref<10xf32>) -> () {
  // expected-error@+1{{requires the same shape for all operands}}
  "lmhlo.map"(%arg0, %arg1, %arg_out) ({
    ^bb0(%a: tensor<f32>, %b: tensor<f32>):
    %c = mhlo.add %a, %b : tensor<f32>
    "mhlo.return"(%c) : (tensor<f32>) -> ()
  }) {dimensions = dense<0> : tensor<1xi64>} : (memref<20xf32>, memref<20xf32>, memref<10xf32>) -> ()
  return
}

// -----

// CHECK-LABEL: func @rng_get_and_update_state_memrefs
func @rng_get_and_update_state_memrefs(%state: memref<1xui64>) -> () {
  "lmhlo.rng_get_and_update_state"(%state) { delta = 1 : i64 } : (memref<1xui64>) -> ()
  return
}

// -----

// CHECK-LABEL: func @sort_memrefs
func @sort_memrefs(%arg0: memref<16x16xf32>, %arg1: memref<16x16xf16>,
                   %out0: memref<16x16xf32>, %out1: memref<16x16xf16>) -> () {
  "lmhlo.sort"(%arg0, %arg1, %out0, %out1) ( {
  ^bb0(%a: tensor<f32>, %b: tensor<f32>, %c: tensor<f16>, %d: tensor<f16>):
    %7 = "mhlo.compare"(%a, %b) {comparison_direction = "GT"} : (tensor<f32>, tensor<f32>) -> tensor<i1>
    "mhlo.return"(%7) : (tensor<i1>) -> ()
  }) {dimension = 1 : i64, is_stable = true} : (memref<16x16xf32>, memref<16x16xf16>, memref<16x16xf32>, memref<16x16xf16>) -> ()
  return
}

// -----

// CHECK-LABEL: func @sort_memrefs
func @sort_memrefs(%arg0: memref<16x16xf32>, %arg1: memref<16x16xf16>,
                   %out0: memref<16x16xf32>, %out1: memref<16x16xf16>) -> () {
  "lmhlo.sort"(%arg0, %arg1, %out0, %out1) ( {
  ^bb0(%a: tensor<f32>, %b: tensor<f32>, %c: tensor<f16>, %d: tensor<f16>):
    %7 = "mhlo.compare"(%a, %b) {comparison_direction = "GT"} : (tensor<f32>, tensor<f32>) -> tensor<i1>
    "mhlo.return"(%7) : (tensor<i1>) -> ()
  }) {dimension = 1 : i64} : (memref<16x16xf32>, memref<16x16xf16>, memref<16x16xf32>, memref<16x16xf16>) -> ()
  return
}

// -----

// CHECK-LABEL: func @sort_memrefs
func @sort_memrefs(%arg0: memref<16x16xf32>, %arg1: memref<16x16xf16>,
                   %out0: memref<16x16xf32>, %out1: memref<16x16xf16>) -> () {
  "lmhlo.sort"(%arg0, %arg1, %out0, %out1) ( {
  ^bb0(%a: tensor<f32>, %b: tensor<f32>, %c: tensor<f16>, %d: tensor<f16>):
    %7 = "mhlo.compare"(%a, %b) {comparison_direction = "GT"} : (tensor<f32>, tensor<f32>) -> tensor<i1>
    "mhlo.return"(%7) : (tensor<i1>) -> ()
  }) : (memref<16x16xf32>, memref<16x16xf16>, memref<16x16xf32>, memref<16x16xf16>) -> ()
  return
}
