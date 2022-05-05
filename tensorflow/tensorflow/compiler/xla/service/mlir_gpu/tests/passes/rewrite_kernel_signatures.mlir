// RUN: xla-mlir-gpu-opt --mlir-gpu-rewrite-signatures %s --split-input-file --verify-diagnostics | FileCheck %s

module attributes {gpu.container_module} {

// CHECK-LABEL: @kernel_module
gpu.module @kernel_module {
  // CHECK-LABEL: gpu.func @kernel
  // CHECK-SAME: %{{.*}}: memref<32xf32>, %{{.*}}: memref<16xf32>,
  // CHECK-SAME: %{{.*}}: memref<8xf32>
  gpu.func @kernel(%arg0: memref<8xf32>, %arg1: memref<16xf32>,
                   %arg2: memref<32xf32>) kernel {
    gpu.return
  }
}

  // CHECK-LABEL: @caller
func @caller(%arg0: memref<32xf32>, %arg1: memref<16xf32>) -> memref<8xf32> {
  %cst = constant 8 : index
  %res = alloc() : memref<8xf32>

  // CHECK: gpu.launch_func
  // CHECK-SAME: index, memref<32xf32>, memref<16xf32>, memref<8xf32>)
  "gpu.launch_func"(%cst, %cst, %cst, %cst, %cst, %cst, %res, %arg1, %arg0)
      { kernel = @kernel_module::@kernel }
      : (index, index, index, index, index, index,
         memref<8xf32>, memref<16xf32>, memref<32xf32>) -> ()

  return %res : memref<8xf32>
}

}

// -----

module attributes {gpu.container_module} {

gpu.module @kernel_module {
  // expected-error @+1 {{number of kernel arguments does not match numberof arguments and results of surrounding function}}
  gpu.func @kernel(%arg0: memref<16xf32>, %arg1: memref<32xf32>) kernel {
    gpu.return
  }
}

func @caller(%arg0: memref<32xf32>, %arg1: memref<16xf32>) -> memref<8xf32> {
  %cst = constant 8 : index
  %res = alloc() : memref<8xf32>

  "gpu.launch_func"(%cst, %cst, %cst, %cst, %cst, %cst, %arg1, %arg0)
      { kernel = @kernel_module::@kernel }
      : (index, index, index, index, index, index,
         memref<16xf32>, memref<32xf32>) -> ()

  return %res : memref<8xf32>
}

}

// -----

module attributes {gpu.container_module} {

gpu.module @kernel_module {
  // expected-error @+1 {{result 0 of containing function is not an argument to the kernel}}
  gpu.func @kernel(%arg0: memref<16xf32>, %arg1: memref<32xf32>,
                   %arg2: memref<8xf32>) kernel {
    gpu.return
  }
}

func @caller(%arg0: memref<32xf32>, %arg1: memref<16xf32>) -> memref<8xf32> {
  %cst = constant 8 : index
  %res = alloc() : memref<8xf32>
  %fake = alloc() : memref<8xf32>

  "gpu.launch_func"(%cst, %cst, %cst, %cst, %cst, %cst, %arg1, %arg0, %fake)
      { kernel = @kernel_module::@kernel }
      : (index, index, index, index, index, index,
         memref<16xf32>, memref<32xf32>, memref<8xf32>) -> ()

  return %res : memref<8xf32>
}

}

// -----

module attributes {gpu.container_module} {

gpu.module @kernel_module {
  // expected-error @+1 {{argument 1 to containing function is not an argument to the kernel}}
  gpu.func @kernel(%arg0: memref<16xf32>, %arg1: memref<32xf32>,
                   %arg2: memref<8xf32>) kernel {
    gpu.return
  }
}

func @caller(%arg0: memref<32xf32>, %arg1: memref<16xf32>) -> memref<8xf32> {
  %cst = constant 8 : index
  %res = alloc() : memref<8xf32>
  %fake = alloc() : memref<16xf32>

  "gpu.launch_func"(%cst, %cst, %cst, %cst, %cst, %cst, %fake, %arg0, %res)
      { kernel = @kernel_module::@kernel }
      : (index, index, index, index, index, index,
         memref<16xf32>, memref<32xf32>, memref<8xf32>) -> ()

  return %res : memref<8xf32>
}

}

// -----

module attributes {gpu.container_module} {

gpu.module @kernel_module {
  gpu.func @kernel(%arg0: memref<8xf32>, %arg1: memref<16xf32>,
                   %arg2: memref<32xf32>) kernel {
    gpu.return
  }
}

// expected-error @+1 {{surrounding function has more than one block}}
func @caller(%arg0: memref<32xf32>, %arg1: memref<16xf32>) -> memref<8xf32> {
  %cst = constant 8 : index
  %res = alloc() : memref<8xf32>
  br ^bb1

  ^bb1:
  "gpu.launch_func"(%cst, %cst, %cst, %cst, %cst, %cst, %res, %arg1, %arg0)
      { kernel = @kernel_module::@kernel }
      : (index, index, index, index, index, index,
         memref<8xf32>, memref<16xf32>, memref<32xf32>) -> ()

  return %res : memref<8xf32>
}

}
