# Copyright 2020 The TensorFlow Authors. All Rights Reserved.
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

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import os
import threading

from tensorflow.python.distribute.parallel_device import parallel_device
from tensorflow.python.eager import backprop
from tensorflow.python.eager import context
from tensorflow.python.eager import def_function
from tensorflow.python.framework import config
from tensorflow.python.framework import constant_op
from tensorflow.python.framework import ops
from tensorflow.python.module import module
from tensorflow.python.ops import array_ops
from tensorflow.python.ops import collective_ops
from tensorflow.python.ops import control_flow_ops
from tensorflow.python.ops import math_ops
from tensorflow.python.ops import variables
from tensorflow.python.platform import test
from tensorflow.python.saved_model import load
from tensorflow.python.saved_model import save
from tensorflow.python.training import checkpoint_management
from tensorflow.python.training.tracking import util as tracking
from tensorflow.python.util import nest

# When running collectives asynchronously, we need to give each parallel device
# execution a unique ID so the collectives don't interfere. Since the op is
# replicated with group/instance key intact, the replicated nodes will
# communicate.
# TODO(allenl): Switch to using a collective manager.
_COUNTER_LOCK = threading.Lock()
_COUNTER = 100


def _collective_reduce(inputs, operation, num_replicas):

  def _reduce_tensor(tensor):
    with _COUNTER_LOCK:
      global _COUNTER
      keys = _COUNTER
      _COUNTER += 1
    return collective_ops.all_reduce(
        t=tensor,
        group_size=num_replicas,
        merge_op=operation,
        group_key=keys,
        instance_key=keys,
        final_op="Id")

  return nest.map_structure(_reduce_tensor, inputs)


def _collective_sum(inputs, num_replicas):
  return _collective_reduce(
      inputs=inputs, operation="Add", num_replicas=num_replicas)


class _Dense(module.Module):

  def __init__(self, output_size):
    self.output_size = output_size
    self.kernel = None
    self.bias = None

  def __call__(self, x):
    if self.kernel is None:
      self.kernel = variables.Variable(
          array_ops.ones(
              array_ops.stack([self.output_size,
                               array_ops.shape(x)[-1]])))
      self.bias = variables.Variable(array_ops.ones([self.output_size]))
    return math_ops.matmul(x, self.kernel, transpose_b=True) + self.bias


class _VirtualDeviceTestCase(test.TestCase):

  def setUp(self):
    super(_VirtualDeviceTestCase, self).setUp()
    ctx = context.context()
    if ctx.list_physical_devices("TPU"):
      self.device_type = "TPU"
    elif ctx.list_physical_devices("GPU"):
      self.device_type = "GPU"
      gpus = ctx.list_physical_devices(self.device_type)
      ctx.set_logical_device_configuration(gpus[0], [
          context.LogicalDeviceConfiguration(memory_limit=100),
          context.LogicalDeviceConfiguration(memory_limit=100),
      ])
    else:
      self.device_type = "CPU"
      cpus = ctx.list_physical_devices("CPU")
      ctx.set_logical_device_configuration(cpus[0], [
          context.LogicalDeviceConfiguration(),
          context.LogicalDeviceConfiguration(),
      ])

    self.device = parallel_device.ParallelDevice(components=[
        "/job:localhost/device:{}:0".format(self.device_type),
        self.device_type + ":1"
    ])
    self.assertIn(self.device_type + ":0", self.device.components[0])
    self.assertIn(self.device_type + ":1", self.device.components[1])


class ParallelDeviceTests(_VirtualDeviceTestCase):

  def test_register_parallel_device(self):
    with self.device:
      c = constant_op.constant(1.)
      d = constant_op.constant(2.)
      e = c + d
      outputs = self.device.unpack(e)
    self.assertAllClose([3., 3.], outputs)

    self.assertIn(self.device.components[0], outputs[0].backing_device)
    self.assertIn(self.device.components[1], outputs[1].backing_device)

  def test_device_id(self):
    device_ids = self.device.unpack(self.device.device_ids)
    self.assertAllClose([0, 1], device_ids)
    # TODO(allenl): Should device IDs be int64 so they can be placed on GPUs?
    # Currently backing_device is CPU.
    self.assertIn(self.device.components[0], device_ids[0].device)
    self.assertIn(self.device.components[1], device_ids[1].device)

  def test_collective_reduce(self):
    if self.device_type == "TPU":
      self.skipTest("ParallelDevice collectives on TPUs need work")
    with self.device:
      x = self.device.pack(
          [constant_op.constant(-1.5),
           constant_op.constant(3.5)])
      reduced = _collective_sum(x, num_replicas=2)
      outputs = self.device.unpack(reduced)
    self.assertAllClose([2., 2.], outputs)
    self.assertIn(self.device.components[0], outputs[0].backing_device)
    self.assertIn(self.device.components[1], outputs[1].backing_device)

  def test_collective_reduce_async_scope(self):
    if self.device_type == "TPU":
      self.skipTest("ParallelDevice collectives on TPUs need work")
    # Note that ops on the parallel device currently don't execute
    # asynchronously. The test is just that we don't get deadlocks.
    with context.async_scope(), self.device:
      x = self.device.pack(
          [constant_op.constant(-1.5),
           constant_op.constant(3.5)])
      reduced = _collective_sum(x, num_replicas=2)
      outputs = self.device.unpack(reduced)
    self.assertAllClose([2., 2.], outputs)
    self.assertIn(self.device.components[0], outputs[0].backing_device)
    self.assertIn(self.device.components[1], outputs[1].backing_device)

  def test_collective_reduce_async_context(self):
    if self.device_type == "TPU":
      self.skipTest("ParallelDevice collectives on TPUs need work")
    previous = config.get_synchronous_execution()
    try:
      context._reset_context()
      config.set_synchronous_execution(False)
      self.setUp()
      # Note that ops on the parallel device currently don't execute
      # asynchronously. The test is just that we don't get deadlocks.
      with self.device:
        x = self.device.pack(
            [constant_op.constant(-1.5),
             constant_op.constant(3.5)])
        reduced = _collective_sum(x, num_replicas=2)
        outputs = self.device.unpack(reduced)
      self.assertAllClose([2., 2.], outputs)
      self.assertIn(self.device.components[0], outputs[0].backing_device)
      self.assertIn(self.device.components[1], outputs[1].backing_device)
    finally:
      context._reset_context()
      config.set_synchronous_execution(previous)

  def test_collective_in_function(self):
    if self.device_type == "TPU":
      self.skipTest("ParallelDevice collectives on TPUs need work")
    c = constant_op.constant([2])

    @def_function.function
    def broadcast_send_recv(device_id):

      @def_function.function
      def send():
        s0 = collective_ops.broadcast_send(
            c * 3, c.shape, c.dtype, group_size=2, group_key=1, instance_key=1)
        with ops.control_dependencies([s0.op]):
          return array_ops.identity(c)

      @def_function.function
      def recv():
        r0 = collective_ops.broadcast_recv(
            c.shape, c.dtype, group_size=2, group_key=1, instance_key=1)
        return r0

      return control_flow_ops.switch_case(
          device_id, branch_fns={0: send, 1: recv})

    with self.device:
      result = broadcast_send_recv(self.device.device_ids)
    self.assertAllClose([[2], [6]], self.device.unpack(result))

  def test_use_in_graph_error_is_informative(self):
    @def_function.function
    def uses_parallel():
      with self.device:
        return self.device.unpack(array_ops.ones([]))

    with self.assertRaisesRegex(NotImplementedError, "inside `tf.function`"):
      uses_parallel()

  def test_checkpointing(self):
    prefix = os.path.join(self.get_temp_dir(), "ckpt")
    with self.device:
      different_values = self.device.pack(
          [constant_op.constant(-1.),
           constant_op.constant(3.)])
      v = variables.Variable(different_values)
      checkpoint = tracking.Checkpoint(v=v)
    save_path = checkpoint.save(prefix)
    with self.device:
      v.assign(constant_op.constant(0.))
    checkpoint.restore(save_path).assert_consumed()
    with self.device:
      outputs = self.device.unpack(v)
    self.assertAllClose([-1., 3.], outputs)

    with self.device:
      restore_on_create = tracking.Checkpoint()
      restore_on_create.restore(save_path)
      restore_on_create.v = variables.Variable(0.)
      outputs = self.device.unpack(restore_on_create.v)
    self.assertAllClose([-1., 3.], outputs)

    # Changing the number of devices / restoring into a single-device copy is OK
    single_device = tracking.Checkpoint(v=variables.Variable(0.))
    status = single_device.restore(save_path)
    status.assert_existing_objects_matched()
    self.assertAllClose(-1., single_device.v)
    with self.assertRaisesRegex(AssertionError, "parallel_component_1"):
      # There are parts of the variable that aren't restored into a
      # single-device copy.
      status.assert_consumed()

  def test_saved_model(self):
    with self.device:
      different_values = self.device.pack(
          [constant_op.constant(-1.),
           constant_op.constant(3.)])
      m = module.Module()
      m.v = variables.Variable(different_values)
      m.f = def_function.function(lambda: m.v * 2.)
      self.assertAllClose([-2., 6.], self.device.unpack(m.f()))
    saved_model_path = os.path.join(self.get_temp_dir(), "saved_model")
    save.save(m, saved_model_path)

    context._reset_context()
    self.setUp()

    single_device_loaded = load.load(saved_model_path)
    self.assertAllClose(-2., single_device_loaded.f())
    with self.device:
      parallel_loaded = load.load(saved_model_path)
      self.assertAllClose([-2., 6.], self.device.unpack(parallel_loaded.f()))
      self.assertAllClose([-1., 3.], self.device.unpack(parallel_loaded.v))
      parallel_loaded.v.assign(self.device.pack([.1, .2]))
      self.assertAllClose([.2, .4], self.device.unpack(parallel_loaded.f()))

  def _assert_close_to_non_parallel(self, computation):
    """Asserts that replication of `computation` works and is equivalent."""
    with self.device:
      parallel_result = computation()
    non_parallel_result = computation()
    # The computations should have the same number and structure of Tensor
    # objects, even though the tensors themselves will be on different devices
    # and represent different numbers of values.
    nest.assert_same_structure(parallel_result, non_parallel_result)
    non_parallel_flat = nest.flatten(non_parallel_result)
    parallel_flat = nest.flatten(parallel_result)
    self.assertGreater(len(parallel_flat), 0)
    for non_parallel, parallel in zip(non_parallel_flat, parallel_flat):
      self.assertEqual(self.device._name, parallel.device)
      self.assertNotEqual(self.device._name, non_parallel.device)
      for parallel_component in self.device.unpack(parallel):
        self.assertAllClose(non_parallel, parallel_component)

  def test_capturing(self):
    with self.device:
      x = constant_op.constant([1., 2.])
      x = array_ops.identity(x)

      @def_function.function
      def f(y):
        return x + y

      y = array_ops.ones([2])
      parallel_result = f(y)
    self.assertAllClose([[2., 3.]] * 2, self.device.unpack(parallel_result))

  def test_euclidean_norm(self):
    def _test_fn():
      with backprop.GradientTape() as tape:
        x = array_ops.ones([5, 5])
        tape.watch(x)
        y = math_ops.reduce_euclidean_norm(x, axis=constant_op.constant(1))
      return y, tape.gradient(y, x)
    self._assert_close_to_non_parallel(_test_fn)

  def test_reduce_sum(self):
    def _test_fn():
      with backprop.GradientTape() as tape:
        x = array_ops.ones([5, 5])
        tape.watch(x)
        y = math_ops.reduce_sum(x, axis=constant_op.constant(1))
      return y, tape.gradient(y, x)
    self._assert_close_to_non_parallel(_test_fn)

  def test_variable_created_in_function(self):

    class M(module.Module):

      def __init__(self):
        self.v = None
        self.w = None
        self.x = None
        self.z = None

      @def_function.function(autograph=False)
      def __call__(self, x):
        if self.v is None:
          with ops.init_scope():
            initial_value = constant_op.constant(2.)
            self.z = variables.Variable(initial_value)
          self.x = variables.Variable(initial_value)
          self.w = variables.Variable(lambda: constant_op.constant(2.))
          self.v = variables.Variable(constant_op.constant(2.))
        return x * self.v * self.w * self.x * self.z

    with self.device:
      m = M()
      packed_outputs = m(array_ops.ones([]))
      outputs = self.device.unpack(packed_outputs)
    self.assertAllClose([16., 16.], outputs)


class LayerTests(_VirtualDeviceTestCase):

  def test_layer_forward(self):
    with self.device:
      layer = _Dense(5)
      x = constant_op.constant([[2.]])
      y = layer(x)
      outputs = self.device.unpack(y)
    self.assertAllClose([[3.] * 5], outputs[0])
    self.assertAllClose([[3.] * 5], outputs[1])
    self.assertIn(self.device.components[0], outputs[0].backing_device)
    self.assertIn(self.device.components[1], outputs[1].backing_device)

    # With different Layer inputs we get different outputs
    with self.device:
      x = self.device.pack(
          [constant_op.constant([[-0.5]]),
           constant_op.constant([[0.5]])])
      y = layer(x)
      outputs = self.device.unpack(y)
    self.assertGreater(
        math_ops.reduce_max(math_ops.abs(outputs[0] - outputs[1])), 1e-5)
    self.assertIn(self.device.components[0], outputs[0].backing_device)
    self.assertIn(self.device.components[1], outputs[1].backing_device)

  def test_layer_sync_training(self):
    if self.device_type == "TPU":
      self.skipTest("ParallelDevice collectives on TPUs need work")
    with self.device:
      layer = _Dense(5)

      with backprop.GradientTape() as tape:
        x = self.device.pack(
            [constant_op.constant([[-0.5]]),
             constant_op.constant([[0.5]])])
        y = layer(x)
        loss = (y - math_ops.range(5.))**2.
      parameters = layer.trainable_variables
      unreduced_gradients = tape.gradient(loss, parameters)
      reduced_gradients = _collective_sum(unreduced_gradients, num_replicas=2)
      for grad, param in zip(reduced_gradients, parameters):
        param.assign_sub(0.01 * grad)
    final_kernels = self.device.unpack(layer.kernel)
    self.assertAllClose(final_kernels[0], final_kernels[1])
    final_bias = self.device.unpack(layer.bias)
    expected_bias = (1. - 0.01 * 2. * (1. + .5 - math_ops.range(5.)) -
                     0.01 * 2. * (1. - .5 - math_ops.range(5.)))
    self.assertAllClose(expected_bias, final_bias[0])
    self.assertAllClose(expected_bias, final_bias[1])
    self.assertIn(self.device.components[0], final_kernels[0].backing_device)
    self.assertIn(self.device.components[1], final_kernels[1].backing_device)

  def test_layer_divergent_buffer_training(self):
    with self.device:
      layer = _Dense(5)

      with backprop.GradientTape() as tape:
        x = self.device.pack(
            [constant_op.constant([[-0.5]]),
             constant_op.constant([[0.5]])])
        y = layer(x)
        loss = (y - math_ops.range(5.))**2.
      parameters = layer.trainable_variables
      unreduced_gradients = tape.gradient(loss, parameters)
      for grad, param in zip(unreduced_gradients, parameters):
        param.assign_sub(0.01 * grad)
    final_kernels = self.device.unpack(layer.kernel)
    self.assertNotAllClose(final_kernels[0], final_kernels[1])
    final_bias = self.device.unpack(layer.bias)
    self.assertAllClose(1. - 0.01 * 2. * (1. - .5 - math_ops.range(5.)),
                        final_bias[0])
    self.assertAllClose(1. - 0.01 * 2. * (1. + .5 - math_ops.range(5.)),
                        final_bias[1])
    self.assertIn(self.device.components[0], final_kernels[0].backing_device)
    self.assertIn(self.device.components[1], final_kernels[1].backing_device)

  def test_training_loop(self):
    if self.device_type == "TPU":
      self.skipTest("ParallelDevice collectives on TPUs need work")
    for _ in range(5):
      layer = _Dense(5)
      checkpoint = tracking.Checkpoint(layer=layer)
      manager = checkpoint_management.CheckpointManager(
          checkpoint, directory=self.get_temp_dir(), max_to_keep=5)
      manager.restore_or_initialize()

      for _ in range(10):
        with self.device:
          with backprop.GradientTape() as tape:
            x = self.device.pack(
                [constant_op.constant([[-0.5]]),
                 constant_op.constant([[0.5]])])
            y = layer(x)
            loss = (y - math_ops.range(5.))**2.
          parameters = layer.trainable_variables
          unreduced_gradients = tape.gradient(loss, parameters)
          reduced_gradients = _collective_sum(
              unreduced_gradients, num_replicas=len(self.device.components))
          for grad, param in zip(reduced_gradients, parameters):
            param.assign_sub(0.01 * grad)

        manager.save()


if __name__ == "__main__":
  ops.enable_eager_execution()
  test.main()
