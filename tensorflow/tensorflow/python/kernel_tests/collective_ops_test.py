# Copyright 2020 The TensorFlow Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ==============================================================================
"""Tests for V2 Collective Operations."""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import threading
import time

from absl.testing import parameterized

from tensorflow.python.compat import v2_compat
from tensorflow.python.data.experimental.ops import testing as dataset_testing
from tensorflow.python.data.ops import dataset_ops
from tensorflow.python.distribute import combinations
from tensorflow.python.distribute import test_util
from tensorflow.python.eager import context
from tensorflow.python.eager import def_function
from tensorflow.python.framework import constant_op
from tensorflow.python.framework import errors
from tensorflow.python.framework import ops
from tensorflow.python.ops import array_ops
from tensorflow.python.ops import collective_ops as _collective_ops
from tensorflow.python.platform import test


class CollectiveOpsV1(object):
  all_reduce = _collective_ops.all_reduce
  all_gather = _collective_ops.all_gather


class CollectiveOpsV2(object):

  @staticmethod
  def all_reduce(t, group_size, group_key, instance_key, *args, **kwargs):
    group_size = array_ops.identity(group_size)
    group_key = array_ops.identity(group_key)
    instance_key = array_ops.identity(instance_key)
    return _collective_ops.all_reduce_v2(t, group_size, group_key, instance_key,
                                         *args, **kwargs)

  @staticmethod
  def all_gather(t, group_size, group_key, instance_key, *args, **kwargs):
    group_size = array_ops.identity(group_size)
    group_key = array_ops.identity(group_key)
    instance_key = array_ops.identity(instance_key)
    return _collective_ops.all_gather_v2(t, group_size, group_key, instance_key,
                                         *args, **kwargs)


device_combination = (
    combinations.combine(device='CPU', communication='RING', required_gpus=0) +
    combinations.combine(
        device='GPU', communication=['RING', 'NCCL'], required_gpus=2))


@combinations.generate(
    combinations.times(
        combinations.combine(
            collective_ops=[
                combinations.NamedObject('v1', CollectiveOpsV1),
                combinations.NamedObject('v2', CollectiveOpsV2)
            ],
            mode='eager'), device_combination))
class CollectiveOpsTest(test.TestCase, parameterized.TestCase):

  def setUp(self):
    _setup_context()
    super().setUp()

  def testReduce(self, collective_ops, device, communication):
    dev0 = '/device:%s:0' % device
    dev1 = '/device:%s:1' % device

    @def_function.function
    def run_all_reduce_1device():
      with ops.device(dev0):
        in_value = constant_op.constant([1.])
        group_size = 1
        group_key = 1
        instance_key = 1
        return collective_ops.all_reduce(
            in_value,
            group_size,
            group_key,
            instance_key,
            communication_hint=communication)

    @def_function.function
    def run_all_reduce_2devices():
      in_value = constant_op.constant([1.])
      group_size = 2
      group_key = 2
      instance_key = 2
      collectives = []
      with ops.device(dev0):
        collectives.append(
            collective_ops.all_reduce(
                in_value,
                group_size,
                group_key,
                instance_key,
                communication_hint=communication))
      with ops.device(dev1):
        collectives.append(
            collective_ops.all_reduce(
                in_value,
                group_size,
                group_key,
                instance_key,
                communication_hint=communication))
      return collectives

    self.assertAllClose(run_all_reduce_1device(), [1.], rtol=1e-5, atol=1e-5)
    for result in run_all_reduce_2devices():
      self.assertAllClose(result, [2.], rtol=1e-5, atol=1e-5)

  def testGather(self, collective_ops, device, communication):
    dev0 = '/device:%s:0' % device
    dev1 = '/device:%s:1' % device

    @def_function.function
    def run_all_gather_1device():
      with ops.device(dev0):
        in_value = constant_op.constant([1.])
        group_size = 1
        group_key = 1
        instance_key = 1
        return collective_ops.all_gather(
            in_value,
            group_size,
            group_key,
            instance_key,
            communication_hint=communication)

    @def_function.function
    def run_all_gather_2devices():
      in_value = constant_op.constant([1.])
      group_size = 2
      group_key = 2
      instance_key = 2
      collectives = []
      with ops.device(dev0):
        collectives.append(
            collective_ops.all_gather(
                in_value,
                group_size,
                group_key,
                instance_key,
                communication_hint=communication))
      with ops.device(dev1):
        collectives.append(
            collective_ops.all_gather(
                in_value,
                group_size,
                group_key,
                instance_key,
                communication_hint=communication))
      return collectives

    self.assertAllClose(run_all_gather_1device(), [1.], rtol=1e-5, atol=1e-5)
    for result in run_all_gather_2devices():
      self.assertAllClose(result, [1., 1.], rtol=1e-5, atol=1e-5)

  def testInstanceKeyScopedUnderGroupKey(self, collective_ops, device,
                                         communication):
    if device == 'GPU' and context.num_gpus() < 4:
      self.skipTest('not enough GPU')

    dev0 = '/device:%s:0' % device
    dev1 = '/device:%s:1' % device
    dev2 = '/device:%s:2' % device
    dev3 = '/device:%s:3' % device

    @def_function.function
    def run_all_reduce_4devices_same_instance_key():
      # Use a common instance key for both groups.
      instance_key = 0
      # We will create 2 groups each with 2 devices.
      group_size = 2
      # Group 0 comprises dev0 and dev1.
      group0_key = 0
      # Group 1 comprises dev2 and dev3.
      group1_key = 1
      collectives = []
      with ops.device(dev0):
        collectives.append(
            collective_ops.all_reduce(
                constant_op.constant(1.), group_size, group0_key, instance_key))
      with ops.device(dev1):
        collectives.append(
            collective_ops.all_reduce(
                constant_op.constant(2.), group_size, group0_key, instance_key))
      with ops.device(dev2):
        collectives.append(
            collective_ops.all_reduce(
                constant_op.constant(3.), group_size, group1_key, instance_key))
      with ops.device(dev3):
        collectives.append(
            collective_ops.all_reduce(
                constant_op.constant(4.), group_size, group1_key, instance_key))
      return collectives

    results = run_all_reduce_4devices_same_instance_key()
    self.assertAllClose(results[0], 3., rtol=1e-5, atol=1e-5)
    self.assertAllClose(results[1], 3., rtol=1e-5, atol=1e-5)
    self.assertAllClose(results[2], 7., rtol=1e-5, atol=1e-5)
    self.assertAllClose(results[3], 7., rtol=1e-5, atol=1e-5)

  def testCollectiveGroupSizeOne(self, collective_ops, device, communication):
    if communication == 'NCCL':
      self.skipTest('b/170672646: it crashes with NCCL and group size one')
    dev0 = '/device:%s:0' % device

    group_size = 1
    group_key = 100
    instance_key = 100
    in_value = [1., 2., 3., 4.]
    in_tensor = constant_op.constant(in_value)

    with ops.device(dev0):
      reduced_tensor = collective_ops.all_reduce(
          in_tensor,
          group_size,
          group_key,
          instance_key,
          communication_hint=communication)
    self.assertAllEqual(in_value, reduced_tensor.numpy())

    with ops.device(dev0):
      gathered_tensor = collective_ops.all_gather(
          in_tensor,
          group_size,
          group_key,
          instance_key,
          communication_hint=communication)
    self.assertAllEqual(in_value, gathered_tensor.numpy())

  def testMultipleGroups(self, collective_ops, device, communication):
    if device == 'GPU' and context.num_gpus() < 4:
      self.skipTest('not enough GPU')

    num_elements = 4

    @def_function.function
    def run_all_reduce(group_size, group_key):
      instance_key = group_key
      input_value = [float(group_key) for i in range(num_elements)]
      collectives = []
      for device_idx in range(group_size):
        with ops.device('/{}:{}'.format(device, device_idx)):
          input_tensor = constant_op.constant(input_value)
          collectives.append(
              collective_ops.all_reduce(
                  input_tensor,
                  group_size,
                  group_key,
                  instance_key,
                  communication_hint=communication))
      return collectives

    def run_and_assert(group_size, group_key):
      for reduced_tensor in run_all_reduce(group_size, group_key):
        self.assertAllEqual(
            [float(group_key) * group_size for i in range(num_elements)],
            reduced_tensor.numpy())

    run_and_assert(group_size=2, group_key=1)
    run_and_assert(group_size=3, group_key=2)


@combinations.generate(
    combinations.times(
        combinations.combine(
            collective_op=[
                combinations.NamedObject('all_reduce',
                                         CollectiveOpsV1.all_reduce),
                combinations.NamedObject('all_reduce_v2',
                                         CollectiveOpsV2.all_reduce),
                combinations.NamedObject('all_gather',
                                         CollectiveOpsV1.all_gather),
                combinations.NamedObject('all_gather_v2',
                                         CollectiveOpsV2.all_gather),
            ],
            mode='eager'), device_combination))
class AbortCollectiveOpsTest(test.TestCase, parameterized.TestCase):

  def setUp(self):
    _setup_context()
    super().setUp()

  def testAbortGroupParamsResolution(self, collective_op, device,
                                     communication):
    dev0 = '/device:%s:0' % device
    dev1 = '/device:%s:1' % device
    group_size = 2
    group_key = 100
    instance_key = 100
    in_tensor = constant_op.constant([1.])

    def abort_fn():
      time.sleep(2)
      context.context().abort_collective_ops(errors.UNAVAILABLE, 'peer down')

    t = threading.Thread(target=abort_fn)
    t.start()

    with self.assertRaisesRegex(errors.UnavailableError, 'peer down'):
      # This hangs on params resolution since we're only launching one
      # collective for a group size of 2.
      with ops.device(dev0):
        collective_op(
            in_tensor,
            group_size,
            group_key,
            instance_key,
            communication_hint=communication)

    # After abortion, subsequent collectives should fail immediately.
    with self.assertRaisesRegex(errors.UnavailableError, 'peer down'):
      with ops.device(dev0):
        collective_op(
            in_tensor,
            group_size,
            group_key,
            instance_key,
            communication_hint=communication)

    t.join()
    # Reset the context in order to reset the collective executor.
    _setup_context()

    # After reset non-NCCL collectives should work.
    def collective_fn():
      for device in [dev0, dev1]:
        with ops.device(device):
          collective_op(
              in_tensor,
              group_size,
              group_key,
              instance_key,
              communication_hint=communication)

    def_function.function(collective_fn)()

  def testAbortInstanceParamsResolution(self, collective_op, device,
                                        communication):
    dev0 = '/device:%s:0' % device
    dev1 = '/device:%s:1' % device
    group_size = 2
    group_key = 100
    instance_key = 100
    in_tensor = constant_op.constant([1.])

    def collective_fn():
      for device in [dev0, dev1]:
        with ops.device(device):
          collective_op(
              in_tensor,
              group_size,
              group_key,
              instance_key,
              communication_hint=communication)

    # First perform a normal all-reduce to complete the group resolution.
    def_function.function(collective_fn)()

    def abort_fn():
      time.sleep(2)
      context.context().abort_collective_ops(errors.UNAVAILABLE, 'peer down')

    t = threading.Thread(target=abort_fn)
    t.start()

    # Use a different instance key to trigger another instance resolution.
    instance_key = 101
    with self.assertRaisesRegex(errors.UnavailableError, 'peer down'):
      # This hangs on params resolution since we're only launching one
      # collective for a group size of 2.
      with ops.device(dev0):
        collective_op(
            in_tensor,
            group_size,
            group_key,
            instance_key,
            communication_hint=communication)

    # After abortion, subsequent collectives should fail immediately.
    with self.assertRaisesRegex(errors.UnavailableError, 'peer down'):
      with ops.device(dev0):
        collective_op(
            in_tensor,
            group_size,
            group_key,
            instance_key,
            communication_hint=communication)

    context._reset_context()  # pylint: disable=protected-access
    t.join()
    # Reset the context in order to reset the collective executor.
    _setup_context()

    # After reset non-NCCL collectives should work.
    def_function.function(collective_fn)()

  def testAbortCommunication(self, collective_op, device, communication):
    dev0 = '/device:%s:0' % device
    dev1 = '/device:%s:1' % device
    group_size = 2
    group_key = 100
    instance_key = 100
    in_tensor = constant_op.constant([1.])

    # First perform a normal collective to finish resolution.
    def collective_fn():
      for device in [dev0, dev1]:
        with ops.device(device):
          collective_op(
              in_tensor,
              group_size,
              group_key,
              instance_key,
              communication_hint=communication)

    def_function.function(collective_fn)()

    # Launch a collective that hangs, and abort the collective executor after
    # the launch.
    def abort_fn():
      time.sleep(2)
      context.context().abort_collective_ops(errors.UNAVAILABLE, 'peer down')

    t = threading.Thread(target=abort_fn)
    t.start()

    with self.assertRaisesRegex(errors.UnavailableError, 'peer down'):
      with ops.device(dev0):
        collective_op(
            in_tensor,
            group_size,
            group_key,
            instance_key,
            communication_hint=communication)

    # After abortion, subsequent collectives should fail immediately.
    with self.assertRaisesRegex(errors.UnavailableError, 'peer down'):
      with ops.device(dev0):
        collective_op(
            in_tensor,
            group_size,
            group_key,
            instance_key,
            communication_hint=communication)

    # Reset the context in order to reset the collective executor.
    t.join()
    _setup_context()
    def_function.function(collective_fn)()

  def testOpErrorNotAbort(self, collective_op, device, communication):
    # Do not abort if there's no active collective ops. There could be
    # exceptions like EOF which we expect users to catch, aborting collective
    # ops on all op errors intervenes with this workflow.
    dev0 = '/device:%s:0' % device
    dev1 = '/device:%s:1' % device
    group_size = 2
    group_key = 100
    instance_key = 100
    dataset = dataset_ops.Dataset.from_tensors([1.])

    @def_function.function
    def collective_fn(in_tensor):
      for device in [dev0, dev1]:
        with ops.device(device):
          collective_op(
              in_tensor,
              group_size,
              group_key,
              instance_key,
              communication_hint=communication)

    @def_function.function
    def f():
      iterator = iter(dataset)
      collective_fn(next(iterator))
      # This next(iterator) should raise EOF.
      collective_fn(next(iterator))

    with self.assertRaises(errors.OutOfRangeError):
      f()
    collective_fn(constant_op.constant([1.]))

  def testOpErrorAbort(self, collective_op, device, communication):
    # Abort collective ops if there're active collective ops at the time of an
    # op error. This is due to the inability to cancel collective ops, and op
    # errors may cause running collective ops to hang.
    dev0 = '/device:%s:0' % device
    group_size = 2
    group_key = 100
    instance_key = 100
    in_tensor = constant_op.constant([1.])
    # Make the dataset sleep a while so that the collective is being executed
    # when the EOF happens.
    dataset = dataset_ops.Dataset.from_tensors([1.]).apply(
        dataset_testing.sleep(sleep_microseconds=200))

    @def_function.function
    def f():
      # Launch a collective op that won't be able to finish to test abortion
      # when other ops error.
      with ops.device(dev0):
        ret = collective_op(
            in_tensor,
            group_size,
            group_key,
            instance_key,
            communication_hint=communication)
      iterator = iter(dataset)
      next(iterator)
      # This should raise EOF.
      next(iterator)
      return ret

    with self.assertRaises(errors.OutOfRangeError):
      f()
    # Now collective ops is aborted, subsequent collective ops should fail with
    # the previous error.
    with self.assertRaises(errors.CancelledError):
      with ops.device(dev0):
        collective_op(
            in_tensor,
            group_size,
            group_key,
            instance_key,
            communication_hint=communication)


@combinations.generate(
    combinations.times(
        combinations.combine(
            collective_op=[
                combinations.NamedObject('all_reduce',
                                         CollectiveOpsV1.all_reduce),
                combinations.NamedObject('all_reduce_v2',
                                         CollectiveOpsV2.all_reduce),
                combinations.NamedObject('all_gather',
                                         CollectiveOpsV1.all_gather),
                combinations.NamedObject('all_gather_v2',
                                         CollectiveOpsV2.all_gather),
            ],
            mode='eager'), device_combination))
class TimeoutTest(test.TestCase, parameterized.TestCase):

  def setUp(self):
    _setup_context()
    super().setUp()

  def testTimeout(self, collective_op, device, communication):
    timeout = 1.5

    @def_function.function
    def run(group_size, reported_group_size=None):
      group_key = 20
      instance_key = 30
      tensor = [1., 2., 3., 4.]
      results = []
      if reported_group_size is None:
        reported_group_size = group_size
      for i in range(group_size):
        with ops.device('/{}:{}'.format(device, i)):
          input_data = constant_op.constant(tensor)
          result = collective_op(
              input_data,
              group_size=reported_group_size,
              group_key=group_key,
              instance_key=instance_key,
              communication_hint=communication,
              timeout=timeout)
          results.append(result)
      return results

    run(2, 2)

    start_time = time.time()
    with self.assertRaisesRegex(errors.DeadlineExceededError,
                                'Collective has timed out during execution'):
      run(1, 2)
    elapsed = time.time() - start_time
    self.assertAllGreaterEqual(elapsed, timeout)

  def testParamResolutionAfterTimeout(self, collective_op, device,
                                      communication):
    dev0 = '/device:%s:0' % device
    dev1 = '/device:%s:1' % device
    timeout = 1.5
    group_key = 20
    instance_key = 30
    input_data = constant_op.constant([1., 2., 3., 4.])

    # This timeout comes from param solution.
    with self.assertRaisesRegex(
        errors.DeadlineExceededError,
        'Collective has timed out waiting for other workers'):
      with ops.device(dev0):
        collective_op(
            input_data,
            group_size=2,
            group_key=group_key,
            instance_key=instance_key,
            communication_hint=communication,
            timeout=timeout)

    # We launch the second device after the first device times out. This is to
    # simulate the situation when other workers are slow and the timeout is
    # short. It should error immediately.
    with self.assertRaisesRegex(
        errors.DeadlineExceededError,
        'Collective has timed out waiting for other workers'):
      with ops.device(dev1):
        collective_op(
            input_data,
            group_size=2,
            group_key=group_key,
            instance_key=instance_key,
            communication_hint=communication)

  def testExecutionAfterTimeout(self, collective_op, device, communication):
    dev0 = '/device:%s:0' % device
    dev1 = '/device:%s:1' % device
    timeout = 1.5
    group_key = 20
    instance_key = 30
    input_data = constant_op.constant([1., 2., 3., 4.])

    @def_function.function
    def run():
      for device in [dev0, dev1]:
        with ops.device(device):
          collective_op(
              input_data,
              group_size=2,
              group_key=group_key,
              instance_key=instance_key,
              communication_hint=communication,
              timeout=timeout)

    # Run a normal all-reduce to complete param resolution.
    run()

    with self.assertRaisesRegex(errors.DeadlineExceededError,
                                'Collective has timed out during execution'):
      with ops.device(dev0):
        collective_op(
            input_data,
            group_size=2,
            group_key=group_key,
            instance_key=instance_key,
            communication_hint=communication,
            timeout=timeout)

    # We launch the second device after the first device times out. This is to
    # simulate the situation when other workers are slow and the timeout is
    # short. It should error immediately.
    with self.assertRaisesRegex(errors.DeadlineExceededError,
                                'Collective has timed out during execution'):
      with ops.device(dev1):
        # No timeout.
        collective_op(
            input_data,
            group_size=2,
            group_key=group_key,
            instance_key=instance_key,
            communication_hint=communication)


def _setup_context():
  context._reset_context()
  test_util.set_logical_devices_to_at_least('CPU', 4)
  context.ensure_initialized()


if __name__ == '__main__':
  v2_compat.enable_v2_behavior()
  test.main()
