# Copyright 2019 The TensorFlow Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ==============================================================================
"""Tests for ShardedVariable."""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import os

from tensorflow.python.client import session as session_lib
from tensorflow.python.compat import v2_compat
from tensorflow.python.distribute import sharded_variable
from tensorflow.python.eager import def_function
from tensorflow.python.framework import constant_op
from tensorflow.python.framework import dtypes
from tensorflow.python.framework import ops
from tensorflow.python.framework import sparse_tensor
from tensorflow.python.framework import tensor_shape
from tensorflow.python.framework import tensor_spec
from tensorflow.python.keras.engine import base_layer
from tensorflow.python.module import module
from tensorflow.python.ops import array_ops
from tensorflow.python.ops import embedding_ops
from tensorflow.python.ops import variable_scope
from tensorflow.python.ops import variables as variables_lib
from tensorflow.python.platform import test
from tensorflow.python.saved_model import loader
from tensorflow.python.saved_model import save
from tensorflow.python.saved_model import signature_constants
from tensorflow.python.saved_model import tag_constants
from tensorflow.python.training.tracking import tracking
from tensorflow.python.training.tracking import util
from tensorflow.python.util import nest


def _load_and_run(
    model_dir,
    inputs,
    signature_key=signature_constants.DEFAULT_SERVING_SIGNATURE_DEF_KEY):
  """Load a SavedModel into a TF 1.x-style graph and run `signature_key`."""
  graph = ops.Graph()
  with graph.as_default(), session_lib.Session() as session:
    meta_graph_def = loader.load(session, [tag_constants.SERVING], model_dir)
    signature = meta_graph_def.signature_def[signature_key]
    feed_dict = {}
    for arg_name in inputs.keys():
      input_tensor = session.graph.get_tensor_by_name(
          signature.inputs[arg_name].name)
      feed_dict[input_tensor] = inputs[arg_name]
    output_dict = {}
    for output_name, output_tensor_info in signature.outputs.items():
      output_dict[output_name] = session.graph.get_tensor_by_name(
          output_tensor_info.name)
    return session.run(output_dict, feed_dict=feed_dict)


class PartitionerTest(test.TestCase):

  def test_fixed_shards_partitioner(self):
    partitioner = sharded_variable.FixedShardsPartitioner(num_shards=2)
    got = partitioner(tensor_shape.TensorShape([10, 3]), dtypes.float32)
    self.assertAllEqual(got, [2, 1])

  def test_min_size_partitioner(self):
    partitioner = sharded_variable.MinSizePartitioner(
        min_shard_bytes=4, max_shards=2)
    got = partitioner(tensor_shape.TensorShape([6, 1]), dtypes.float32)
    self.assertAllEqual(got, [2, 1])

    partitioner = sharded_variable.MinSizePartitioner(
        min_shard_bytes=4, max_shards=10)
    got = partitioner(tensor_shape.TensorShape([6, 1]), dtypes.float32)
    self.assertAllEqual(got, [6, 1])

  def test_max_size_partitioner(self):
    partitioner = sharded_variable.MaxSizePartitioner(max_shard_bytes=4)
    got = partitioner(tensor_shape.TensorShape([6, 1]), dtypes.float32)
    self.assertAllEqual(got, [6, 1])

    partitioner = sharded_variable.MaxSizePartitioner(
        max_shard_bytes=4, max_shards=2)
    got = partitioner(tensor_shape.TensorShape([6, 1]), dtypes.float32)
    self.assertAllEqual(got, [2, 1])

    partitioner = sharded_variable.MaxSizePartitioner(max_shard_bytes=1024)
    got = partitioner(tensor_shape.TensorShape([6, 1]), dtypes.float32)
    self.assertAllEqual(got, [1, 1])


class ShardedVariableTest(test.TestCase):

  def test_sharded_variable_simple(self):
    v0 = variables_lib.Variable([0])
    v1 = variables_lib.Variable([1])
    s = sharded_variable.ShardedVariable([v0, v1], name='s')
    self.assertEqual(s.variables[0], v0)
    self.assertEqual(s.variables[1], v1)
    self.assertEqual(s.shape.as_list(), [2])
    self.assertEqual(s.dtype, v0.dtype)
    self.assertEqual(s.name, 's')

  def test_assign(self):
    v0 = variables_lib.Variable([[0, 0]])
    v1 = variables_lib.Variable([[1, 1], [2, 2]])
    v2 = variables_lib.Variable([[3, 3]])
    s = sharded_variable.ShardedVariable([v0, v1, v2])
    s.assign([[4, 4], [5, 5], [6, 6], [7, 7]])
    self.assertAllEqual(self.evaluate(s.variables[0]), [[4, 4]])
    self.assertAllEqual(self.evaluate(s.variables[1]), [[5, 5], [6, 6]])
    self.assertAllEqual(self.evaluate(s.variables[2]), [[7, 7]])

  def test_assign_add(self):
    v0 = variables_lib.Variable([[0, 0]])
    v1 = variables_lib.Variable([[1, 1], [2, 2]])
    v2 = variables_lib.Variable([[3, 3]])
    s = sharded_variable.ShardedVariable([v0, v1, v2])
    s.assign_add([[1, 1], [1, 1], [2, 2], [2, 2]])
    self.assertAllEqual(self.evaluate(s.variables[0]), [[1, 1]])
    self.assertAllEqual(self.evaluate(s.variables[1]), [[2, 2], [4, 4]])
    self.assertAllEqual(self.evaluate(s.variables[2]), [[5, 5]])

  def test_assign_sub(self):
    v0 = variables_lib.Variable([[0, 0]])
    v1 = variables_lib.Variable([[1, 1], [2, 2]])
    v2 = variables_lib.Variable([[3, 3]])
    s = sharded_variable.ShardedVariable([v0, v1, v2])
    s.assign_sub([[0, 0], [1, 1], [1, 1], [3, 3]])
    self.assertAllEqual(self.evaluate(s.variables[0]), [[0, 0]])
    self.assertAllEqual(self.evaluate(s.variables[1]), [[0, 0], [1, 1]])
    self.assertAllEqual(self.evaluate(s.variables[2]), [[0, 0]])

  def test_convert_to_tensor(self):
    v0 = variables_lib.Variable([[0, 0]])
    v1 = variables_lib.Variable([[1, 1], [2, 2]])
    v2 = variables_lib.Variable([[3, 3]])
    s = sharded_variable.ShardedVariable([v0, v1, v2])
    t = ops.convert_to_tensor(s)
    self.assertAllEqual(t, [[0, 0], [1, 1], [2, 2], [3, 3]])

  def test_save_restore(self):
    fname = os.path.join(self.get_temp_dir(), 'checkpoint')
    variables = [
        variables_lib.Variable([0]),
        variables_lib.Variable([1]),
        variables_lib.Variable([2]),
        variables_lib.Variable([3])
    ]
    s = sharded_variable.ShardedVariable(variables, name='s')

    cp = util.Checkpoint(s=s)
    self.assertEqual(self.evaluate(cp.s.variables[0]), [0])
    cp.write(fname)

    self.evaluate(cp.s.variables[0].assign([4]))
    self.assertEqual(self.evaluate(cp.s.variables[0]), [4])

    cp.restore(fname)
    # Tests that the original weights are restored.
    self.assertEqual(self.evaluate(cp.s.variables[0]), [0])

  def test_save_restore_different_partitions(self):
    fname = os.path.join(self.get_temp_dir(), 'checkpoint')
    variables = [
        variables_lib.Variable([0]),
        variables_lib.Variable([1]),
        variables_lib.Variable([2]),
        variables_lib.Variable([3])
    ]
    s = sharded_variable.ShardedVariable(variables, name='s')

    cp = util.Checkpoint(s=s)
    cp.write(fname)

    variables2 = [variables_lib.Variable([0, 0, 0, 0])]
    s2 = sharded_variable.ShardedVariable(variables2, name='s')

    # Restore from 4 partitions into 1.
    cp2 = util.Checkpoint(s=s2)
    cp2.restore(fname)
    self.assertAllEqual(self.evaluate(cp2.s.variables[0]), [0, 1, 2, 3])

    self.evaluate(cp2.s.variables[0].assign([5, 10, 15, 20]))
    cp2.write(fname)

    # Restore 1 partition into 4.
    cp.restore(fname)
    self.assertEqual(self.evaluate(cp.s.variables[0]), [5])
    self.assertEqual(self.evaluate(cp.s.variables[1]), [10])
    self.assertEqual(self.evaluate(cp.s.variables[2]), [15])
    self.assertEqual(self.evaluate(cp.s.variables[3]), [20])

  def test_save_restore_4_to_2_partitions(self):
    fname = os.path.join(self.get_temp_dir(), 'checkpoint')
    variables = [
        variables_lib.Variable([0]),
        variables_lib.Variable([1]),
        variables_lib.Variable([2]),
        variables_lib.Variable([3])
    ]
    s = sharded_variable.ShardedVariable(variables, name='s')
    cp = util.Checkpoint(s=s)
    cp.write(fname)

    variables2 = [
        variables_lib.Variable([0, 0]),
        variables_lib.Variable([0, 0])
    ]
    s2 = sharded_variable.ShardedVariable(variables2, name='s')
    cp2 = util.Checkpoint(s=s2)
    cp2.restore(fname)
    # Assert that weights from the 4 partitions were loaded here.
    self.assertLen(cp2.s.variables, 2)
    self.assertAllEqual(self.evaluate(cp2.s.variables[0]), [0, 1])
    self.assertAllEqual(self.evaluate(cp2.s.variables[1]), [2, 3])

  def test_delayed_restore(self):
    fname = os.path.join(self.get_temp_dir(), 'checkpoint')
    model = tracking.AutoTrackable()
    variables = [
        variables_lib.Variable([0]),
        variables_lib.Variable([1]),
        variables_lib.Variable([2]),
        variables_lib.Variable([3])
    ]
    model.s = sharded_variable.ShardedVariable(variables)
    cp = util.Checkpoint(model=model)
    cp.write(fname)

    model2 = tracking.AutoTrackable()
    cp2 = util.Checkpoint(model=model2)
    cp2.restore(fname)
    variables2 = [
        variables_lib.Variable([0]),
        variables_lib.Variable([0]),
        variables_lib.Variable([0]),
        variables_lib.Variable([0])
    ]
    model2.s = sharded_variable.ShardedVariable(variables2)
    self.assertAllEqual(self.evaluate(model2.s.variables[0]), [0])
    self.assertAllEqual(self.evaluate(model2.s.variables[1]), [1])
    self.assertAllEqual(self.evaluate(model2.s.variables[2]), [2])
    self.assertAllEqual(self.evaluate(model2.s.variables[3]), [3])

  def test_delayed_restore_4_to_2_partitions(self):
    fname = os.path.join(self.get_temp_dir(), 'checkpoint')
    model = tracking.AutoTrackable()
    variables = [
        variables_lib.Variable([0]),
        variables_lib.Variable([1]),
        variables_lib.Variable([2]),
        variables_lib.Variable([3])
    ]
    model.s = sharded_variable.ShardedVariable(variables)
    cp = util.Checkpoint(model=model)
    cp.write(fname)

    model2 = tracking.AutoTrackable()
    cp2 = util.Checkpoint(model=model2)
    cp2.restore(fname)
    variables2 = [
        variables_lib.Variable([0, 0]),
        variables_lib.Variable([0, 0])
    ]
    model2.s = sharded_variable.ShardedVariable(variables2)
    self.assertAllEqual(self.evaluate(model2.s.variables[0]), [0, 1])
    self.assertAllEqual(self.evaluate(model2.s.variables[1]), [2, 3])

  def test_save_graph_def(self):
    root = tracking.AutoTrackable()
    v1 = variables_lib.Variable([3.])
    v2 = variables_lib.Variable([2.])
    root.v = sharded_variable.ShardedVariable([v1, v2])
    root.train = def_function.function(
        lambda x: embedding_ops.embedding_lookup_v2(root.v.variables, x))
    # TODO(b/144057383): Remove the necessity of root.serve once saving context
    # is made to tf.function cache.
    root.serve = def_function.function(
        lambda x: embedding_ops.embedding_lookup_v2(root.v.variables[0], x),
        input_signature=[tensor_spec.TensorSpec([2], dtypes.int32, name='x')])

    # Trace and use root.train
    self.assertAllEqual([3., 2.], root.train([0, 1]).numpy())

    save_dir = os.path.join(self.get_temp_dir(), 'saved_model')
    save.save(root, save_dir, root.serve)
    self.assertAllEqual([3., 2.],
                        _load_and_run(save_dir, {'x': [0, 1]})['output_0'])

    # Continue using root.train for training
    self.assertAllEqual([3., 2.], root.train([0, 1]).numpy())

  def test_validation_errors(self):
    with self.assertRaisesRegex(ValueError, 'Expected a list of '):
      sharded_variable.ShardedVariable(
          [variables_lib.Variable([0]), 'not-a-variable'])

    with self.assertRaisesRegex(ValueError, 'must have the same dtype'):
      sharded_variable.ShardedVariable([
          variables_lib.Variable([0], dtype='int64'),
          variables_lib.Variable([1], dtype='int32')
      ])

    with self.assertRaisesRegex(ValueError, 'the same shapes except'):
      sharded_variable.ShardedVariable([
          variables_lib.Variable(array_ops.ones((5, 10))),
          variables_lib.Variable(array_ops.ones((5, 20)))
      ])

    with self.assertRaisesRegex(ValueError, '`SaveSliceInfo` should not'):
      v = variables_lib.Variable([0])
      v._set_save_slice_info(
          variables_lib.Variable.SaveSliceInfo(
              full_name='s', full_shape=[2], var_offset=[0], var_shape=[1]))
      sharded_variable.ShardedVariable([v])

  def test_as_function_input(self):
    variables1 = [
        variables_lib.Variable([1]),
        variables_lib.Variable([1]),
    ]
    s = sharded_variable.ShardedVariable(variables1)
    variables2 = [
        variables_lib.Variable([2]),
        variables_lib.Variable([2]),
    ]
    s2 = sharded_variable.ShardedVariable(variables2)

    trace_count = [0]

    @def_function.function
    def func(sharded_var):
      trace_count[0] = trace_count[0] + 1
      sharded_var.assign([0, 0])

    func(s)
    self.assertAllEqual(ops.convert_to_tensor(s), [0, 0])
    self.assertEqual(trace_count[0], 1)
    func(s2)
    self.assertAllEqual(ops.convert_to_tensor(s2), [0, 0])
    self.assertEqual(trace_count[0], 1)

  def test_flatten(self):
    variables = [
        variables_lib.Variable([0]),
        variables_lib.Variable([1]),
    ]
    s = sharded_variable.ShardedVariable(variables)

    got = nest.flatten(s)
    self.assertEqual(s, got[0])

    got = nest.flatten(s, expand_composites=True)
    self.assertAllEqual(variables, got)

  def test_tf_module(self):

    class Model(module.Module):

      def __init__(self):
        super().__init__()
        variables = [
            variables_lib.Variable([0]),
            variables_lib.Variable([1]),
        ]
        self.w = sharded_variable.ShardedVariable(variables)

    model = Model()

    self.assertLen(model.variables, 2)
    self.assertEqual(model.variables[0], [0])
    self.assertEqual(model.variables[1], [1])
    self.assertAllEqual(model.variables, model.trainable_variables)

    self.assertLen(model._checkpoint_dependencies, 1)
    self.assertEqual(model._checkpoint_dependencies[0].ref, model.w)

  def test_keras_layer_setattr(self):

    class Layer(base_layer.Layer):

      def __init__(self):
        super().__init__()
        variables1 = [
            variables_lib.Variable([0]),
            variables_lib.Variable([1]),
        ]
        variables2 = [
            variables_lib.Variable([2], trainable=False),
            variables_lib.Variable([3], trainable=False),
        ]
        self.w = sharded_variable.ShardedVariable(variables1)
        self.b = sharded_variable.ShardedVariable(variables2)

    layer = Layer()

    self.assertLen(layer.trainable_weights, 2)
    self.assertEqual(layer.trainable_weights[0], [0])
    self.assertEqual(layer.trainable_weights[1], [1])
    self.assertLen(layer.non_trainable_weights, 2)
    self.assertEqual(layer.non_trainable_weights[0], [2])
    self.assertEqual(layer.non_trainable_weights[1], [3])
    self.assertAllEqual(layer.weights,
                        layer.trainable_weights + layer.non_trainable_weights)
    self.assertAllEqual(layer.trainable_weights, layer.trainable_variables)
    self.assertAllEqual(layer.weights, layer.variables)

    checkpoint_deps = set(dep.ref for dep in layer._checkpoint_dependencies)
    self.assertEqual(checkpoint_deps, set([layer.w, layer.b]))

  def test_keras_layer_add_weight(self):

    class Layer(base_layer.Layer):

      def __init__(self):
        super().__init__()
        self.w = self.add_weight(
            shape=(2,), initializer=lambda shape, dtype: [0, 1], trainable=True)
        self.b = self.add_weight(
            shape=(2,),
            initializer=lambda shape, dtype: [2, 3],
            trainable=False)

    def sharded_variable_creator(next_creator, **kwargs):
      v1_value = kwargs['initial_value']()[0:1]
      v2_value = kwargs['initial_value']()[1:]

      kwargs['initial_value'] = v1_value
      kwargs['shape'] = (1,)
      v1 = next_creator(**kwargs)

      kwargs['initial_value'] = v2_value
      kwargs['shape'] = (1,)
      v2 = next_creator(**kwargs)

      return sharded_variable.ShardedVariable([v1, v2])

    with variable_scope.variable_creator_scope(sharded_variable_creator):
      layer = Layer()

    self.assertLen(layer.trainable_weights, 2)
    self.assertEqual(layer.trainable_weights[0], [0])
    self.assertEqual(layer.trainable_weights[1], [1])
    self.assertLen(layer.non_trainable_weights, 2)
    self.assertEqual(layer.non_trainable_weights[0], [2])
    self.assertEqual(layer.non_trainable_weights[1], [3])
    self.assertAllEqual(layer.weights,
                        layer.trainable_weights + layer.non_trainable_weights)
    self.assertAllEqual(layer.trainable_weights, layer.trainable_variables)
    self.assertAllEqual(layer.weights, layer.variables)

    checkpoint_deps = set(dep.ref for dep in layer._checkpoint_dependencies)
    self.assertEqual(checkpoint_deps, set([layer.w, layer.b]))

  def test_embedding_lookup(self):
    v = [
        variables_lib.Variable([[1., 2.], [3., 4.]]),
        variables_lib.Variable([[5., 6.], [7., 8.]]),
        variables_lib.Variable([[9., 10.]])
    ]
    sv = sharded_variable.ShardedVariable(v)

    @def_function.function
    def lookup():
      ids = constant_op.constant([0, 3, 4])
      return embedding_ops.embedding_lookup_v2(sv, ids)

    @def_function.function
    def sparse_lookup():
      sp_ids = sparse_tensor.SparseTensor(
          indices=[[0, 0], [0, 1], [1, 0], [2, 2]],
          values=[0, 3, 4, 1],
          dense_shape=[3, 3])
      return embedding_ops.embedding_lookup_sparse_v2(sv, sp_ids, None)

    @def_function.function
    def safe_sparse_lookup():
      sp_ids = sparse_tensor.SparseTensor(
          indices=[[0, 0], [0, 1], [1, 0], [2, 2]],
          values=[0, -1, 4, 1],
          dense_shape=[3, 3])
      sp_weights = sparse_tensor.SparseTensor(
          indices=[[0, 0], [0, 1], [1, 0], [2, 2]],
          values=[1., 1., -1., 1.],
          dense_shape=[3, 3])
      return embedding_ops.safe_embedding_lookup_sparse_v2(
          sv, sp_ids, sp_weights)

    # TODO(chenkai): Add safe_sparse_lookup to the list. Currently
    # ShardedVariable is converted to a tensor in safe_sparse_lookup.
    for func in [lookup, sparse_lookup]:
      num_gather_ops = 0
      for op in func.get_concrete_function().graph.get_operations():
        if op.type == 'ResourceGather':
          num_gather_ops += 1
      self.assertEqual(
          num_gather_ops, len(v), 'Number of ResourceGather op does not match'
          ' expected, possibly due to ShardedVariable accidentally being'
          ' converted to tensor in embedding_lookup ops.')

    self.assertAllEqual(lookup(), [[1., 2.], [7., 8.], [9., 10.]])
    self.assertAllClose(sparse_lookup(), [[4., 5.], [9., 10.], [3., 4.]])
    self.assertAllClose(safe_sparse_lookup(), [[1., 2.], [0., 0.], [3., 4.]])


if __name__ == '__main__':
  v2_compat.enable_v2_behavior()
  test.main()
