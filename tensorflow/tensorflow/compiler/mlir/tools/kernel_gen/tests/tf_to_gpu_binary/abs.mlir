// RUN: tf_to_gpu_binary --input=%s --output=%t --same_shape=0,1 --unroll_factors=4 --tile_sizes=256 --arch=sm_70
func @abs(%arg0: tensor<?xf16>) -> tensor<?xf16> {
  %0 = "tf.Abs"(%arg0) { }
    : (tensor<?xf16>) -> tensor<?xf16>
  return %0 : tensor<?xf16>
}
