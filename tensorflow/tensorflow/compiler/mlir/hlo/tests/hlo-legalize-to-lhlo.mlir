// RUN: mlir-hlo-opt -hlo-legalize-to-lhlo -buffer-placement -split-input-file %s -o - | FILECHECK_OPTS="" FileCheck --check-prefixes=PRE,BOTH %s
// RUN: mlir-hlo-opt -hlo-legalize-to-lhlo=results-escape-function=true -buffer-placement -split-input-file %s -o - | FILECHECK_OPTS="" FileCheck --check-prefixes=ESC,BOTH %s

// BOTH-LABEL: func @attrs
func @attrs_copy(%operand: memref<2x2xf32>, %result: memref<2x2xf32>) {
  %tensor_operand = tensor_load %operand : memref<2x2xf32>
  %tensor_result = "mhlo.exponential"(%tensor_operand)
      {some_attr_1 = "exp.1", some_attr_2 = dense<1> : tensor<1xi64>}
      : (tensor<2x2xf32>) -> tensor<2x2xf32>
  // BOTH: "lmhlo.exponential"(%{{.*}}, %{{.*}}) {some_attr_1 = "exp.1", some_attr_2 = dense<1> : tensor<1xi64>}
  tensor_store %tensor_result, %result : memref<2x2xf32>
  return
}

// -----

func @return_func(%arg0: tensor<4xf32>) -> tensor<4xf32> {
  return %arg0 : tensor<4xf32>
}
//      PRE: (%[[ARG0:.*]]: [[TYPE:.*]], %[[RESULT:.*]]: [[TYPE]])
// PRE-NEXT: "lmhlo.copy"(%[[ARG0]], %[[RESULT]]) : ([[TYPE]], [[TYPE]]) -> ()
// PRE-NEXT: return
//      ESC: (%[[ARG0:.*]]: [[TYPE:.*]]) -> [[TYPE]]
//  ESC-NOT: "lmhlo.copy"
// ESC-NEXT: return %[[ARG0]]

// -----

// BOTH-LABEL: func @func_op_long
func @func_op_long(%arg0: tensor<4xf32>, %arg1: tensor<4xf32>) -> tensor<4xf32> {
  %1 = mhlo.maximum %arg0, %arg1 : tensor<4xf32>
  %2 = mhlo.add %arg0, %1 : tensor<4xf32>
  %3 = mhlo.minimum %arg0, %arg1 : tensor<4xf32>
  %4 = mhlo.subtract %arg1, %3 : tensor<4xf32>
  %5 = mhlo.multiply %2, %4 : tensor<4xf32>
  return %5 : tensor<4xf32>
}
//        PRE: (%[[NEW_ARG0:.*]]: memref<4xf32>, %[[NEW_ARG1:.*]]: memref<4xf32>, %[[RESULT:.*]]: memref<4xf32>)
//        ESC: (%[[NEW_ARG0:.*]]: memref<4xf32>, %[[NEW_ARG1:.*]]: memref<4xf32>) -> memref<4xf32>
//  BOTH-NEXT: %[[MAX_RESULT:.*]] = alloc() : memref<4xf32>
//  BOTH-NEXT: "lmhlo.maximum"(%[[NEW_ARG0]], %[[NEW_ARG1]], %[[MAX_RESULT]])
//  BOTH-NEXT: %[[ADD_RESULT:.*]] = alloc() : memref<4xf32>
//  BOTH-NEXT: "lmhlo.add"(%[[NEW_ARG0]], %[[MAX_RESULT]], %[[ADD_RESULT]])
//  BOTH-NEXT: dealloc %[[MAX_RESULT]] : memref<4xf32>
//  BOTH-NEXT: %[[MIN_RESULT:.*]] = alloc() : memref<4xf32>
//  BOTH-NEXT: "lmhlo.minimum"(%[[NEW_ARG0]], %[[NEW_ARG1]], %[[MIN_RESULT]])
//  BOTH-NEXT: %[[SUB_RESULT:.*]] = alloc() : memref<4xf32>
//  BOTH-NEXT: "lmhlo.subtract"(%[[NEW_ARG1]], %[[MIN_RESULT]], %[[SUB_RESULT]])
//  BOTH-NEXT: dealloc %[[MIN_RESULT]] : memref<4xf32>
//  BOTH-NEXT: %[[MUL_RESULT:.*]] = alloc() : memref<4xf32>
//  BOTH-NEXT: "lmhlo.multiply"(%[[ADD_RESULT]], %[[SUB_RESULT]], %[[MUL_RESULT]])
//  BOTH-NEXT: dealloc %[[SUB_RESULT]] : memref<4xf32>
//  BOTH-NEXT: dealloc %[[ADD_RESULT]] : memref<4xf32>
//   PRE-NEXT: "lmhlo.copy"(%[[MUL_RESULT]], %[[RESULT]]) : (memref<4xf32>, memref<4xf32>) -> ()
//   PRE-NEXT: dealloc %[[MUL_RESULT]] : memref<4xf32>
//   PRE-NEXT: return
//   ESC-NEXT: return %[[MUL_RESULT]] : memref<4xf32>

// -----

// BOTH-LABEL: func @fusion
func @fusion(%multiplier: memref<2x2xf32>, %summand_1: memref<2x2xf32>,
             %summand_2: memref<2x2xf32>, %result: memref<2x2xf32>) {
  // BOTH: (%{{.*}}: {{.*}}, {{.*}}: {{.*}}, {{.*}}: {{.*}}, %[[RESULT:.*]]: {{.*}})
  // BOTH-NEXT:  %[[ADD_RESULT:.*]] = alloc() : memref<2x2xf32>
  %tensor_summand_1 = tensor_load %summand_1 : memref<2x2xf32>
  %tensor_summand_2 = tensor_load %summand_2 : memref<2x2xf32>
  %sum = "mhlo.add"(%tensor_summand_1, %tensor_summand_2)
      : (tensor<2x2xf32>, tensor<2x2xf32>) -> tensor<2x2xf32>
  // BOTH-NEXT: "lmhlo.add"(%{{.*}}, %{{.*}}, %[[ADD_RESULT]])
  // BOTH-NEXT:  %[[MUL_RESULT:.*]] = alloc() : memref<2x2xf32>
  %tensor_multiplier = tensor_load %multiplier : memref<2x2xf32>
  %tensor_result = "mhlo.multiply"(%sum, %tensor_multiplier)
      : (tensor<2x2xf32>, tensor<2x2xf32>) -> tensor<2x2xf32>
  // BOTH-NEXT: "lmhlo.multiply"(%[[ADD_RESULT]], %{{.*}}, %[[MUL_RESULT]])
  // BOTH-NEXT:  dealloc %[[ADD_RESULT]] : memref<2x2xf32>
  // BOTH-NEXT: "lmhlo.copy"(%[[MUL_RESULT]], %[[RESULT]])
  tensor_store %tensor_result, %result : memref<2x2xf32>
  // BOTH-NEXT:  dealloc %[[MUL_RESULT]] : memref<2x2xf32>
  // BOTH-NEXT:  return
  return
}

// -----

// BOTH-LABEL: func @copy
func @copy(%operand: memref<2x2xf32>, %result: memref<2x2xf32>) {
  %tensor_operand = tensor_load %operand : memref<2x2xf32>
  %tensor_result = "mhlo.copy"(%tensor_operand)
      : (tensor<2x2xf32>) -> tensor<2x2xf32>
  // BOTH: "lmhlo.copy"(%{{.*}}, %{{.*}})
  tensor_store %tensor_result, %result : memref<2x2xf32>
  return
}

// -----

// BOTH-LABEL: func @exp
func @exp(%operand: memref<2x2xf32>, %result: memref<2x2xf32>) {
  %tensor_operand = tensor_load %operand : memref<2x2xf32>
  %tensor_result = "mhlo.exponential"(%tensor_operand)
      : (tensor<2x2xf32>) -> tensor<2x2xf32>
  // BOTH: "lmhlo.exponential"(%{{.*}}, %{{.*}})
  tensor_store %tensor_result, %result : memref<2x2xf32>
  return
}

// -----

// BOTH-LABEL: func @log
func @log(%operand: memref<2x2xf32>, %result: memref<2x2xf32>) {
  %tensor_operand = tensor_load %operand : memref<2x2xf32>
  %tensor_result = "mhlo.log"(%tensor_operand)
      : (tensor<2x2xf32>) -> tensor<2x2xf32>
  // BOTH: "lmhlo.log"(%{{.*}}, %{{.*}})
  tensor_store %tensor_result, %result : memref<2x2xf32>
  return
}

// -----

// BOTH-LABEL: func @select
func @select(%pred: memref<2x2xi1>, %lhs: memref<2x2xf32>,
             %rhs: memref<2x2xf32>, %result: memref<2x2xf32>) {
  %tensor_pred = tensor_load %pred : memref<2x2xi1>
  %tensor_lhs = tensor_load %lhs : memref<2x2xf32>
  %tensor_rhs = tensor_load %rhs : memref<2x2xf32>
  %tensor_result = "mhlo.select"(%tensor_pred, %tensor_lhs, %tensor_rhs)
      : (tensor<2x2xi1>, tensor<2x2xf32>, tensor<2x2xf32>) -> tensor<2x2xf32>
  // BOTH: "lmhlo.select"(%{{.*}}, %{{.*}}, %{{.*}}, %{{.*}})
  tensor_store %tensor_result, %result : memref<2x2xf32>
  return
}

// -----

// BOTH-LABEL: func @compare
func @compare(%lhs: memref<2x2xf32>, %rhs: memref<2x2xf32>, %result: memref<2x2xi1>) {
  %tensor_lhs = tensor_load %lhs : memref<2x2xf32>
  %tensor_rhs = tensor_load %rhs : memref<2x2xf32>
  %tensor_result = "mhlo.compare"(%tensor_lhs, %tensor_rhs)
      {comparison_direction = "EQ"}
      : (tensor<2x2xf32>, tensor<2x2xf32>) -> tensor<2x2xi1>
  // BOTH: "lmhlo.compare"(%{{.*}}, %{{.*}}, %{{.*}}) {comparison_direction = "EQ"}
  tensor_store %tensor_result, %result : memref<2x2xi1>
  return
}

// -----

// BOTH-LABEL: func @broadcast
func @broadcast(%operand: memref<5xf32>, %result: memref<10x5xf32>) {
  %tensor_operand = tensor_load %operand : memref<5xf32>
  %tensor_result = "mhlo.broadcast_in_dim"(%tensor_operand)
      {broadcast_dimensions = dense<1> : tensor<1xi64>}
        : (tensor<5xf32>) -> tensor<10x5xf32>
  // BOTH: "lmhlo.broadcast_in_dim"(%{{.*}}, %{{.*}}) {broadcast_dimensions = dense<1> : tensor<1xi64>}
  tensor_store %tensor_result, %result : memref<10x5xf32>
  return
}

// -----

func @external_func() -> tensor<3xi64>

// BOTH: #[[MAP:.*]] = affine_map<(d0, d1)[s0, s1] -> (d0 * s0 + d1 * s1)>

// BOTH-LABEL: func @dyn_broadcast
func @dyn_broadcast(%operand: memref<?x?xf32>) {
  // BOTH-SAME: (%[[OPERAND:.*]]: memref<?x?xf32>)
  %tensor_operand = tensor_load %operand : memref<?x?xf32>
  %c1 = constant 1 : i64
  %shape = tensor_from_elements %c1, %c1, %c1 : tensor<3xi64>
  %tensor_result = "mhlo.dynamic_broadcast_in_dim"(%tensor_operand, %shape) {
    broadcast_dimensions = dense<[1, 2]> : tensor<2xi64>
  } : (tensor<?x?xf32>, tensor<3xi64>) -> tensor<?x?x?xf32>
  // BOTH: %[[SHAPE:.*]] = tensor_from_elements
  // BOTH: %[[C0:.*]] = constant 0 : index
  // BOTH: %[[EL0:.*]] = extract_element %[[SHAPE]][%[[C0]]] : tensor<3xi64>
  // BOTH: %[[IC0:.*]]  = index_cast %[[EL0]] : i64 to index
  // BOTH: %[[C1:.*]] = constant 1 : index
  // BOTH: %[[EL1:.*]] = extract_element %[[SHAPE]][%[[C1]]] : tensor<3xi64>
  // BOTH: %[[IC1:.*]]  = index_cast %[[EL1]] : i64 to index
  // BOTH: %[[C2:.*]] = constant 2 : index
  // BOTH: %[[EL2:.*]] = extract_element %[[SHAPE]][%[[C2]]] : tensor<3xi64>
  // BOTH: %[[IC2:.*]]  = index_cast %[[EL2]] : i64 to index
  // BOTH: %[[RESULT:.*]] = alloc(%[[IC0]], %[[IC1]], %[[IC2]])

  // BOTH: %[[C0_:.*]] = constant 0 : index
  // BOTH: %[[C1_:.*]] = constant 1 : index

  // BOTH: %[[C1__:.*]] = constant 1 : index
  // BOTH: %[[EL1_:.*]] = extract_element %[[SHAPE]]{{\[}}%[[C1__]]] : tensor<3xi64>
  // BOTH: %[[C0___:.*]] = constant 0 : index
  // BOTH: %[[OPERAND_DIM_0:.*]] = dim %[[OPERAND]], %[[C0___]] : memref<?x?xf32>
  // BOTH: %[[RESULT_DIM_1:.*]] = index_cast %[[EL1_]] : i64 to index
  // BOTH: %[[EXPAND_0:.*]] = cmpi "slt", %[[OPERAND_DIM_0]], %[[RESULT_DIM_1]]
  // BOTH: %[[STRIDE_0:.*]] = select %[[EXPAND_0]], %[[C0_]], %[[C1_]] : index

  // BOTH: %[[C2_:.*]] = constant 2 : index
  // BOTH: %[[EL2_:.*]] = extract_element %[[SHAPE]]{{\[}}%[[C2_]]] : tensor<3xi64>
  // BOTH: %[[C1___:.*]] = constant 1 : index
  // BOTH: %[[OPERAND_DIM_1:.*]] = dim %[[OPERAND]], %[[C1___]] : memref<?x?xf32>
  // BOTH: %[[RESULT_DIM_2:.*]] = index_cast %[[EL2_]] : i64 to index
  // BOTH: %[[EXPAND_1:.*]] = cmpi "slt", %[[OPERAND_DIM_1]], %[[RESULT_DIM_2]]
  // BOTH: %[[STRIDE_1:.*]] = select %[[EXPAND_1]], %[[C0_]], %[[C1_]] : index

  // BOTH: %[[TRANSFORMED_MEMREF:.*]] = lmhlo.dynamic_memref_cast
  // BOTH-SAME: %[[OPERAND]](%[[RESULT_DIM_1]], %[[RESULT_DIM_2]])
  // BOTH-SAME: {{\[}}%[[STRIDE_0]], %[[STRIDE_1]]]
  // BOTH-SAME: : memref<?x?xf32> -> memref<?x?xf32, #map0>

  // BOTH: "lmhlo.broadcast_in_dim"(%[[TRANSFORMED_MEMREF]], %[[RESULT]]) {
  // BOTH-SAME:   broadcast_dimensions = dense<[1, 2]> : tensor<2xi64>
  // BOTH-SAME: } : (memref<?x?xf32, #[[MAP]]>, memref<?x?x?xf32>) -> ()

  // Do not store the value back to avoid the tensor-store being rewritten to
  // a copy into the pre-allocated argument.
  return
}

// -----

// BOTH-LABEL: func @complex
func @complex(%real: memref<2x2xf32>,
              %imag: memref<2x2xf32>,
              %result: memref<2x2xcomplex<f32>>) {
  %tensor_real = tensor_load %real : memref<2x2xf32>
  %tensor_imag = tensor_load %imag : memref<2x2xf32>
  %tensor_result = "mhlo.complex"(%tensor_real, %tensor_imag)
      : (tensor<2x2xf32>, tensor<2x2xf32>) -> tensor<2x2xcomplex<f32>>
  // BOTH: "lmhlo.complex"(%{{.*}}, %{{.*}})
  tensor_store %tensor_result, %result : memref<2x2xcomplex<f32>>
  return
}

// -----

// BOTH-LABEL: func @complex_dyn
func @complex_dyn(%real: memref<?xf32>,
                  %imag: memref<?xf32>,
                  %result: memref<?xcomplex<f32>>) {
  %tensor_real = tensor_load %real : memref<?xf32>
  %tensor_imag = tensor_load %imag : memref<?xf32>
  %tensor_result = "mhlo.complex"(%tensor_real, %tensor_imag)
      : (tensor<?xf32>, tensor<?xf32>) -> tensor<?xcomplex<f32>>
  // BOTH: "lmhlo.complex"(%{{.*}}, %{{.*}})
  tensor_store %tensor_result, %result : memref<?xcomplex<f32>>
  return
}

// -----

// BOTH-LABEL: func @real
func @real(%operand: memref<2x2xcomplex<f32>>, %result: memref<2x2xf32>) {
  %tensor_operand = tensor_load %operand : memref<2x2xcomplex<f32>>
  %tensor_result = "mhlo.real"(%tensor_operand)
      : (tensor<2x2xcomplex<f32>>) -> tensor<2x2xf32>
  // BOTH: "lmhlo.real"(%{{.*}}, %{{.*}})
  tensor_store %tensor_result, %result : memref<2x2xf32>
  return
}

// -----

// BOTH-LABEL: func @real_dyn
func @real_dyn(%operand: memref<?xcomplex<f32>>, %result: memref<?xf32>) {
  %tensor_operand = tensor_load %operand : memref<?xcomplex<f32>>
  %tensor_result = "mhlo.real"(%tensor_operand)
      : (tensor<?xcomplex<f32>>) -> tensor<?xf32>
  // BOTH: "lmhlo.real"(%{{.*}}, %{{.*}})
  tensor_store %tensor_result, %result : memref<?xf32>
  return
}

// -----

// BOTH-LABEL: func @imag
func @imag(%operand: memref<2x2xcomplex<f32>>, %result: memref<2x2xf32>) {
  %tensor_operand = tensor_load %operand : memref<2x2xcomplex<f32>>
  %tensor_result = "mhlo.imag"(%tensor_operand)
      : (tensor<2x2xcomplex<f32>>) -> tensor<2x2xf32>
  // BOTH: "lmhlo.imag"(%{{.*}}, %{{.*}})
  tensor_store %tensor_result, %result : memref<2x2xf32>
  return
}

// -----

// BOTH-LABEL: func @gather
func @gather(%operand: memref<13x7xf32>, %idxs: memref<5xi32>, %result: memref<5x7xf32>) {
  %tensor_operand = tensor_load %operand : memref<13x7xf32>
  %tensor_idxs = tensor_load %idxs : memref<5xi32>
  %tensor_result =
    "mhlo.gather"(%tensor_operand, %tensor_idxs)
      { dimension_numbers =
        { collapsed_slice_dims = dense<0> : tensor<1xi64>
        , index_vector_dim = 1 : i64
        , offset_dims = dense<1> : tensor<1xi64>
        , start_index_map = dense<0> : tensor<1xi64> }
      , indices_are_sorted = false
      , name = "gather.71"
      , slice_sizes = dense<[1, 7]> : tensor<2xi64> }
      : (tensor<13x7xf32>, tensor<5xi32>) -> tensor<5x7xf32>
  // BOTH: "lmhlo.gather"(%{{.*}}, %{{.*}}, %{{.*}})
  tensor_store %tensor_result, %result : memref<5x7xf32>
  return
}

// -----

// BOTH-LABEL: func @imag_dyn
func @imag_dyn(%operand: memref<?xcomplex<f32>>, %result: memref<?xf32>) {
  %tensor_operand = tensor_load %operand : memref<?xcomplex<f32>>
  %tensor_result = "mhlo.imag"(%tensor_operand)
      : (tensor<?xcomplex<f32>>) -> tensor<?xf32>
  // BOTH: "lmhlo.imag"(%{{.*}}, %{{.*}})
  tensor_store %tensor_result, %result : memref<?xf32>
  return
}

// -----

// BOTH-LABEL: func @iota
func @iota(%result: memref<10xi32>) {
  %tensor_result = "mhlo.iota"()
      {iota_dimension = 0 : i64} : () -> tensor<10xi32>
  // BOTH: "lmhlo.iota"(%{{.*}}) {iota_dimension = 0 : i64}
  tensor_store %tensor_result, %result : memref<10xi32>
  return
}

// -----

// BOTH-LABEL: func @abs
func @abs(%operand: memref<2x2xf32>, %result: memref<2x2xf32>) {
  %tensor_operand = tensor_load %operand : memref<2x2xf32>
  %tensor_result = "mhlo.abs"(%tensor_operand)
      : (tensor<2x2xf32>) -> tensor<2x2xf32>
  // BOTH: "lmhlo.abs"(%{{.*}}, %{{.*}})
  tensor_store %tensor_result, %result : memref<2x2xf32>
  return
}

// -----

// BOTH-LABEL: func @ceil
func @ceil(%operand: memref<2x2xf32>, %result: memref<2x2xf32>) {
  %tensor_operand = tensor_load %operand : memref<2x2xf32>
  %tensor_result = "mhlo.ceil"(%tensor_operand)
      : (tensor<2x2xf32>) -> tensor<2x2xf32>
  // BOTH: "lmhlo.ceil"(%{{.*}}, %{{.*}})
  tensor_store %tensor_result, %result : memref<2x2xf32>
  return
}

// -----

// BOTH-LABEL: func @convert
func @convert(%operand: memref<2x2xf32>, %result: memref<2x2xf32>) {
  %tensor_operand = tensor_load %operand : memref<2x2xf32>
  %tensor_result = "mhlo.convert"(%tensor_operand)
      : (tensor<2x2xf32>) -> tensor<2x2xf32>
  // BOTH: "lmhlo.copy"(%{{.*}}, %{{.*}})
  // BOTH-NOT: tensor_store
  tensor_store %tensor_result, %result : memref<2x2xf32>
  return
}

// -----

// BOTH-LABEL: func @cos
func @cos(%operand: memref<2x2xf32>, %result: memref<2x2xf32>) {
  %tensor_operand = tensor_load %operand : memref<2x2xf32>
  %tensor_result = "mhlo.cosine"(%tensor_operand)
      : (tensor<2x2xf32>) -> tensor<2x2xf32>
  // BOTH: "lmhlo.cosine"(%{{.*}}, %{{.*}})
  tensor_store %tensor_result, %result : memref<2x2xf32>
  return
}

// -----

// BOTH-LABEL: func @floor
func @floor(%operand: memref<2x2xf32>, %result: memref<2x2xf32>) {
  %tensor_operand = tensor_load %operand : memref<2x2xf32>
  %tensor_result = "mhlo.floor"(%tensor_operand)
      : (tensor<2x2xf32>) -> tensor<2x2xf32>
  // BOTH: "lmhlo.floor"(%{{.*}}, %{{.*}})
  tensor_store %tensor_result, %result : memref<2x2xf32>
  return
}

// -----

// BOTH-LABEL: func @neg
func @neg(%operand: memref<2x2xf32>, %result: memref<2x2xf32>) {
  %tensor_operand = tensor_load %operand : memref<2x2xf32>
  %tensor_result = "mhlo.negate"(%tensor_operand)
      : (tensor<2x2xf32>) -> tensor<2x2xf32>
  // BOTH: "lmhlo.negate"(%{{.*}}, %{{.*}})
  tensor_store %tensor_result, %result : memref<2x2xf32>
  return
}

// -----

// BOTH-LABEL: func @not
func @not(%operand: memref<2x2xi32>, %result: memref<2x2xi32>) {
  %tensor_operand = tensor_load %operand : memref<2x2xi32>
  %tensor_result = "mhlo.not"(%tensor_operand)
      : (tensor<2x2xi32>) -> tensor<2x2xi32>
  // BOTH: "lmhlo.not"(%{{.*}}, %{{.*}})
  tensor_store %tensor_result, %result : memref<2x2xi32>
  return
}

// -----

// BOTH-LABEL: func @rsqrt
func @rsqrt(%operand: memref<2x2xf32>, %result: memref<2x2xf32>) {
  %tensor_operand = tensor_load %operand : memref<2x2xf32>
  %tensor_result = "mhlo.rsqrt"(%tensor_operand)
      : (tensor<2x2xf32>) -> tensor<2x2xf32>
  // BOTH: "lmhlo.rsqrt"(%{{.*}}, %{{.*}})
  tensor_store %tensor_result, %result : memref<2x2xf32>
  return
}

// -----

// BOTH-LABEL: func @sign
func @sign(%operand: memref<2x2xf32>, %result: memref<2x2xf32>) {
  %tensor_operand = tensor_load %operand : memref<2x2xf32>
  %tensor_result = "mhlo.sign"(%tensor_operand)
      : (tensor<2x2xf32>) -> tensor<2x2xf32>
  // BOTH: "lmhlo.sign"(%{{.*}}, %{{.*}})
  tensor_store %tensor_result, %result : memref<2x2xf32>
  return
}

// -----

// BOTH-LABEL: func @sqrt
func @sqrt(%operand: memref<2x2xf32>, %result: memref<2x2xf32>) {
  %tensor_operand = tensor_load %operand : memref<2x2xf32>
  %tensor_result = "mhlo.sqrt"(%tensor_operand)
      : (tensor<2x2xf32>) -> tensor<2x2xf32>
  // BOTH: "lmhlo.sqrt"(%{{.*}}, %{{.*}})
  tensor_store %tensor_result, %result : memref<2x2xf32>
  return
}

// -----

// BOTH-LABEL: func @tanh
func @tanh(%operand: memref<2x2xf32>, %result: memref<2x2xf32>) {
  %tensor_operand = tensor_load %operand : memref<2x2xf32>
  %tensor_result = "mhlo.tanh"(%tensor_operand)
      : (tensor<2x2xf32>) -> tensor<2x2xf32>
  // BOTH: "lmhlo.tanh"(%{{.*}}, %{{.*}})
  tensor_store %tensor_result, %result : memref<2x2xf32>
  return
}

// -----

// BOTH-LABEL: func @remainder
func @remainder(%lhs: memref<2x2xf32>, %rhs: memref<2x2xf32>, %result: memref<2x2xf32>) {
  %tensor_lhs = tensor_load %lhs : memref<2x2xf32>
  %tensor_rhs = tensor_load %rhs : memref<2x2xf32>
  %tensor_result = "mhlo.remainder"(%tensor_lhs, %tensor_rhs)
      : (tensor<2x2xf32>, tensor<2x2xf32>) -> tensor<2x2xf32>
  // BOTH: "lmhlo.remainder"(%{{.*}}, %{{.*}}, %{{.*}})
  tensor_store %tensor_result, %result : memref<2x2xf32>
  return
}

// -----

// Dynamic shape binary element-wise operation.
// BOTH-LABEL: func @add_dyn
func @add_dyn(%lhs: tensor<?x?xf32>, %rhs: tensor<?x?xf32>) {
  %result = "mhlo.add"(%lhs, %rhs)
      : (tensor<?x?xf32>, tensor<?x?xf32>) -> tensor<?x?xf32>
  // BOTH: %[[C0:.*]] = constant 0 : index
  // BOTH: %[[DIM0:.*]] = dim %arg0, %[[C0]] : memref<?x?xf32>
  // BOTH: %[[IC0:.*]] = index_cast %[[DIM0]] : index to i64
  // BOTH: %[[C1:.*]] = constant 1 : index
  // BOTH: %[[DIM1:.*]] = dim %arg0, %[[C1]] : memref<?x?xf32>
  // BOTH: %[[IC1:.*]] = index_cast %[[DIM1]] : index to i64
  // BOTH: %[[SHAPE:.*]] = tensor_from_elements %[[IC0]], %[[IC1]] : tensor<2xi64>
  // BOTH: %[[C0_:.*]] = constant 0 : index
  // BOTH: %[[EE0:.*]] = extract_element %[[SHAPE]][%[[C0_]]] : tensor<2xi64>
  // BOTH: %[[ICS0:.*]] = index_cast %[[EE0]] : i64 to index
  // BOTH: %[[C1_:.*]] = constant 1 : index
  // BOTH: %[[EE1:.*]] = extract_element %[[SHAPE]][%[[C1_]]] : tensor<2xi64>
  // BOTH: %[[ICS1:.*]] = index_cast %[[EE1]] : i64 to index
  // BOTH: %[[RESULT:.*]] = alloc(%[[ICS0]], %[[ICS1]])
  // BOTH: "lmhlo.add"(%arg0, %arg1, %[[RESULT]]) : (memref<?x?xf32>, memref<?x?xf32>, memref<?x?xf32>) -> ()
  return
}

// -----

// Dynamic shape unary element-wise operation.
// BOTH-LABEL: func @tanh_dyn
func @tanh_dyn(%arg0: tensor<?x?xf32>) {
  %result = "mhlo.tanh"(%arg0)
      : (tensor<?x?xf32>) -> tensor<?x?xf32>
  // BOTH: %[[C0:.*]] = constant 0 : index
  // BOTH: %[[DIM0:.*]] = dim %arg0, %[[C0]] : memref<?x?xf32>
  // BOTH: %[[IC0:.*]] = index_cast %[[DIM0]] : index to i64
  // BOTH: %[[C1:.*]] = constant 1 : index
  // BOTH: %[[DIM1:.*]] = dim %arg0, %[[C1]] : memref<?x?xf32>
  // BOTH: %[[IC1:.*]] = index_cast %[[DIM1]] : index to i64
  // BOTH: %[[SHAPE:.*]] = tensor_from_elements %[[IC0]], %[[IC1]] : tensor<2xi64>
  // BOTH: %[[C0_:.*]] = constant 0 : index
  // BOTH: %[[EE0:.*]] = extract_element %[[SHAPE]][%[[C0_]]] : tensor<2xi64>
  // BOTH: %[[ICS0:.*]] = index_cast %[[EE0]] : i64 to index
  // BOTH: %[[C1_:.*]] = constant 1 : index
  // BOTH: %[[EE1:.*]] = extract_element %[[SHAPE]][%[[C1_]]] : tensor<2xi64>
  // BOTH: %[[ICS1:.*]] = index_cast %[[EE1]] : i64 to index
  // BOTH: %[[RESULT:.*]] = alloc(%[[ICS0]], %[[ICS1]])
  // BOTH: "lmhlo.tanh"(%arg0, %[[RESULT]]) : (memref<?x?xf32>, memref<?x?xf32>) -> ()
  return
}

// -----

// BOTH-LABEL: func @dot
func @dot(%arg0: tensor<1024x1024xf32>) -> tensor<1024x1024xf32> {
//  PRE-SAME: (%[[ARG0:.*]]: [[TYPE:.*]], %[[RESULT:.*]]: [[TYPE]])
//  ESC-SAME: (%[[ARG0:.*]]: [[TYPE:.*]]) -> [[TYPE]]
// BOTH-NEXT: %[[ALLOC:.*]] = alloc
//      BOTH: "lmhlo.dot"(%[[ARG0]], %[[ARG0]], %[[ALLOC]]) {
//        dot_dimension_numbers = {
//          lhs_batching_dimensions = dense<> : tensor<0xi64>,
//          lhs_contracting_dimensions = dense<1> : tensor<1xi64>,
//          rhs_batching_dimensions = dense<> : tensor<0xi64>,
//          rhs_contracting_dimensions = dense<0> : tensor<1xi64>}}
//        : ([[TYPE]], [[TYPE]], [[TYPE]]) -> ()
  %dot = "mhlo.dot"(%arg0, %arg0)
          : (tensor<1024x1024xf32>, tensor<1024x1024xf32>) -> tensor<1024x1024xf32>
// PRE: "lmhlo.copy"(%[[ALLOC]], %[[RESULT]])
// ESC: return %[[ALLOC]]
  return %dot : tensor<1024x1024xf32>
}

// -----

// BOTH-LABEL: func @conv
func @conv(%input: tensor<3x5x5x3xf32>, %filter : tensor<2x2x3x4xf32>) -> tensor<3x5x5x4xf32> {
  %c0 = constant 0 : index
  // BOTH: %[[OUT:.*]] = alloc() : memref<3x5x5x4xf32>
  // BOTH: "lmhlo.convolution"(%{{.+}}, %{{.+}}, %[[OUT]])
  // BOTH-SAME: padding = dense<[
  // BOTH-SAME:                  [0, 1], [0, 1]]> : tensor<2x2xi64>
  // BOTH-SAME: rhs_dilation = dense<[1, 2]>
  // BOTH-SAME: window_strides = dense<[2, 1]>
  %out = "mhlo.convolution"(%filter, %input) {
    batch_group_count = 1 : i64,
    dimension_numbers = {
      input_batch_dimension = 0 : i64,
      input_feature_dimension = 3 : i64,
      input_spatial_dimensions = dense<[1, 2]> : tensor<2xi64>,
      kernel_input_feature_dimension = 2 : i64,
      kernel_output_feature_dimension = 3 : i64,
      kernel_spatial_dimensions = dense<[0, 1]> : tensor<2xi64>,
      output_batch_dimension = 0 : i64,
      output_feature_dimension = 3 : i64,
      output_spatial_dimensions = dense<[1, 2]> : tensor<2xi64>
    },
    feature_group_count = 1 : i64,
    padding = dense<[[0, 1], [0, 1]]> : tensor<2x2xi64>,
    rhs_dilation = dense<[1, 2]> : tensor<2xi64>,
    window_strides = dense<[2, 1]> : tensor<2xi64>
  } : (tensor<2x2x3x4xf32>, tensor<3x5x5x3xf32>) -> tensor<3x5x5x4xf32>
  return %out : tensor<3x5x5x4xf32>
}

// -----

// BOTH-LABEL: func @reduce
func @reduce(%arg0: tensor<1x8xf32>, %arg1: tensor<f32>) -> tensor<1xf32> {
  // BOTH: %[[OUT:.*]] = alloc() : memref<1xf32>
  // BOTH:  "lmhlo.reduce"(%{{.+}}, %{{.+}}, %[[OUT]]) ( {
  // BOTH:  ^bb0(%[[ARG1:.*]]: memref<f32>, %[[ARG2:.*]]: memref<f32>,
  // BOTH-SAME:  %[[ARG3:.*]]: memref<f32>):
  // BOTH:    %[[TMP:.*]] = alloc() : memref<f32>
  // BOTH:    "lmhlo.add"(%[[ARG1]], %[[ARG2]], %[[TMP]])
  // BOTH:    "lmhlo.copy"(%[[TMP]], %[[ARG3]])
  // BOTH:    "lmhlo.terminator"() : () -> ()
  // BOTH:  }) {dimensions = dense<1> : tensor<1xi64>}
  // BOTH-SAME: : (memref<1x8xf32>, memref<f32>, memref<1xf32>) -> ()
  %0 = "mhlo.reduce"(%arg0, %arg1) ( {
  ^bb0(%arg2: tensor<f32>, %arg3: tensor<f32>):  // no predecessors
    %1 = mhlo.add %arg2, %arg3 : tensor<f32>
    "mhlo.return"(%1) : (tensor<f32>) -> ()
  }) {dimensions = dense<1> : tensor<1xi64>}
      : (tensor<1x8xf32>, tensor<f32>) -> tensor<1xf32>
  return %0 : tensor<1xf32>
}

// -----

// BOTH-LABEL: func @transpose
func @transpose(%operand: memref<2x2xf32>, %result: memref<2x2xf32>) {
  %tensor_operand = tensor_load %operand : memref<2x2xf32>
  %tensor_result = "mhlo.transpose"(%tensor_operand) {permutation = dense<[1, 0]> : tensor<2xi64>}
                    : (tensor<2x2xf32>) -> tensor<2x2xf32>
  // BOTH: "lmhlo.transpose"(%{{.*}}, %{{.*}}) {permutation = dense<[1, 0]> : tensor<2xi64>}
  // BOTH-NOT: tensor_store
  tensor_store %tensor_result, %result : memref<2x2xf32>
  return
}

// -----

// BOTH-LABEL: func @custom_call
// BOTH-SAME:([[ARG0:%.*]]: memref<2x2xf32>, [[ARG1:%.*]]: memref<2x3xf32>, [[RESULT:%.*]]: memref<4x4xf16>)
func @custom_call(%arg0: memref<2x2xf32>, %arg1: memref<2x3xf32>, %result: memref<4x4xf16>) {
  %arg0_tensor = tensor_load %arg0 : memref<2x2xf32>
  %arg1_tensor = tensor_load %arg1 : memref<2x3xf32>
  // BOTH: "lmhlo.custom_call"([[ARG0]], [[ARG1]], %{{.*}}) {backend_config = "", call_target_name = "foo", has_side_effect = false}
  %result_tensor = "mhlo.custom_call"(%arg0_tensor, %arg1_tensor)
                   {backend_config = "", call_target_name = "foo", has_side_effect = false}
                   : (tensor<2x2xf32>, tensor<2x3xf32>) -> tensor<4x4xf16>
  tensor_store %result_tensor, %result: memref<4x4xf16>
  return
}

// ----

// BOTH-LABEL: func @isfinite
func @isfinite(%arg0: memref<2x2xf32>, %result: memref<2x2xi1>) {
  %arg0_tensor = tensor_load %arg0 : memref<2x2xf32>
  // BOTH: "lmhlo.is_finite"(%{{.*}}, %{{.*}})
  %result_tensor = "mhlo.is_finite"(%arg0_tensor) : (tensor<2x2xf32>) -> tensor<2x2xi1>
  tensor_store %result_tensor, %result: memref<2x2xi1>
  return
}

// -----

// Test that assuming ops propagate memref types.
// BOTH-LABEL: func @shape_assuming_memref
func @shape_assuming_memref(%arg0: tensor<?xf16>) -> tensor<?xf16> {
  %0 = mhlo.constant dense<0.000000e+00> : tensor<f16>
  %1 = shape.const_witness true
  // BOTH: shape.assuming %{{.*}} -> (memref<?xf16>)
  %2 = shape.assuming %1 -> (tensor<?xf16>) {
    %3 = shape.shape_of %arg0 : tensor<?xf16> -> tensor<?xindex>
    %4 = tensor_cast %3 : tensor<?xindex> to tensor<1xindex>
    %5 = "mhlo.dynamic_broadcast_in_dim"(%0, %4) {broadcast_dimensions = dense<> : tensor<0xi64>} : (tensor<f16>, tensor<1xindex>) -> tensor<?xf16>
    %6 = "mhlo.dynamic_broadcast_in_dim"(%arg0, %4) {broadcast_dimensions = dense<0> : tensor<1xi64>} : (tensor<?xf16>, tensor<1xindex>) -> tensor<?xf16>
    // BOTH: "lmhlo.maximum"(%6, %9, %20) : (memref<?xf16>, memref<?xf16>, memref<?xf16>) -> ()
    %7 = mhlo.maximum %5, %6 : tensor<?xf16>
    // BOTH: shape.assuming_yield %{{.*}} : memref<?xf16>
    shape.assuming_yield %7 : tensor<?xf16>
  }
  return %2 : tensor<?xf16>
}
