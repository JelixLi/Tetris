// RUN: tf-mlir-translate -mlir-tf-mlir-to-str-attr %s | FileCheck %s

module attributes {tf.versions = {producer = 888 : i32}} {
  func @main(%arg0: tensor<?xi32>) -> tensor<?xi32> {
    %0 = "tf.Identity"(%arg0) : (tensor<?xi32>) -> tensor<?xi32> loc(unknown)
    return %0 : tensor<?xi32> loc(unknown)
  } loc(unknown)
} loc(unknown)

// CHECK: "\0A\0Amodule attributes {tf.versions = {producer = 888 : i32}} {\0A func @main(%arg0: tensor<?xi32>) -> tensor<?xi32> {\0A %0 = \22tf.Identity\22(%arg0) : (tensor<?xi32>) -> tensor<?xi32> loc(unknown)\0A return %0 : tensor<?xi32> loc(unknown)\0A } loc(unknown)\0A} loc(unknown)"
