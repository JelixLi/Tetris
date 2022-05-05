# Copyright 2018 The TensorFlow Authors. All Rights Reserved.
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
"""This module customizes `test_combinations` for `tf.distribute.Strategy`.

Additionally it provides `generate()`, `combine()` and `times()` with
`tf.distribute.Strategy` customizations as a default.
"""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import collections
import copy
import re
import sys
import types
import unittest

import six

from tensorflow.python.client import session
from tensorflow.python.distribute import collective_all_reduce_strategy
from tensorflow.python.distribute import distribute_lib
from tensorflow.python.distribute import multi_process_runner
from tensorflow.python.distribute import multi_worker_test_base
from tensorflow.python.eager import context
from tensorflow.python.framework import combinations as framework_combinations
from tensorflow.python.framework import ops
from tensorflow.python.framework import test_combinations as combinations_lib
from tensorflow.python.framework import test_util
from tensorflow.python.platform import flags
from tensorflow.python.platform import tf_logging as logging
from tensorflow.python.util import tf_decorator
from tensorflow.python.util import tf_inspect
from tensorflow.python.util.tf_export import tf_export


# TODO(rchao): Rename `distribution` parameter to `strategy` or
# `distribute_strategy` in all tests.
class DistributionParameter(combinations_lib.ParameterModifier):
  """Transforms arguments of type `NamedDistribution`.

  Convert all arguments of type `NamedDistribution` to the value of their
  `strategy` property.
  """

  def modified_arguments(self, kwargs, requested_parameters):
    # Get the parameter that indicates if we need to set the `_use_policy` flag
    # on the strategy object. This is a temporary flag for testing the variable
    # policy rollout.
    use_var_policy = kwargs.get("use_var_policy", None)
    distribution_arguments = {}
    for k, v in kwargs.items():
      if isinstance(v, NamedDistribution):
        strategy = v.strategy
        if use_var_policy:
          strategy.extended._use_var_policy = use_var_policy
        distribution_arguments[k] = strategy
    return distribution_arguments


class ClusterParameters(combinations_lib.ParameterModifier):
  """Adds cluster parameters if a `NamedDistribution` has it.

  It needs to be before DistributionParameter.
  """

  def modified_arguments(self, kwargs, requested_parameters):
    strategy = None
    for _, v in kwargs.items():
      if isinstance(v, NamedDistribution):
        if strategy is not None and _num_total_workers(v.has_chief,
                                                       v.num_workers) > 1:
          raise ValueError("Only support one NamedDistribution for multi worker"
                           "tests.")
        strategy = v

    if strategy:
      has_chief = strategy.has_chief
      num_workers = strategy.num_workers
      runner = strategy.runner
      if "has_chief" in kwargs and kwargs["has_chief"] != has_chief:
        raise ValueError(
            "both has_chief and strategy specified but are not compatible")
      if "num_workers" in kwargs and kwargs["num_workers"] != num_workers:
        raise ValueError(
            "both num_workers and strategy specified but are not compatible")
    else:
      has_chief = kwargs.get("has_chief", False)
      num_workers = kwargs.get("num_workers", 1)
      runner = kwargs.get("runner", None)

    # Always set cluster parameters if they're requested. So that generate()
    # works when there's no startegy in the combinations.
    update = {}
    if "has_chief" in requested_parameters:
      update["has_chief"] = has_chief
    if "num_workers" in requested_parameters:
      update["num_workers"] = num_workers
    if "runner" in requested_parameters:
      update["runner"] = runner
    return update


class DistributionCombination(combinations_lib.TestCombination):
  """Sets up distribution strategy for tests."""

  def should_execute_combination(self, kwargs):
    distributions = [
        v for v in kwargs.values() if isinstance(v, NamedDistribution)
    ]
    if test_util.is_xla_enabled() and any(d.no_xla for d in distributions):
      return (
          False,
          "n/a: skipping strategy combination with no_xla=True in XLA tests")
    return (True, None)

  def parameter_modifiers(self):
    return [
        DistributionParameter(),
        combinations_lib.OptionalParameter("use_var_policy"),
    ]


class ClusterCombination(combinations_lib.TestCombination):
  """Sets up multi worker tests."""

  def parameter_modifiers(self):
    return [ClusterParameters()]


class GPUCombination(combinations_lib.TestCombination):
  """Enable tests to request GPU hardware and skip non-GPU combinations.

  This class expects test_combinations to be generated with `NamedDistribution`
  wrapping instances of `tf.distribute.Strategy`.

  Optionally, the `required_gpus` argument is supported.  GPU hardware is
  required, if its value is `True` or > 0.

  Attributes:
    GPU_TEST: The environment is considered to have GPU hardware available if
              the name of the program contains "test_gpu" or "test_xla_gpu".
  """

  GPU_TEST = re.search(r"(test_gpu|test_xla_gpu)$", sys.argv[0])

  def should_execute_combination(self, kwargs):
    distributions = [
        v for v in kwargs.values() if isinstance(v, NamedDistribution)
    ]
    required_gpus = kwargs.get("required_gpus", None)

    if distributions and required_gpus:
      raise ValueError("Do not use `required_gpus` and arguments of type "
                       "NamedDistribution together.")

    number_of_required_gpus = max([required_gpus or 0] +
                                  [d.required_gpus or 0 for d in distributions])

    if not number_of_required_gpus and GPUCombination.GPU_TEST:
      return (False, "Test that doesn't require GPUs.")
    elif (number_of_required_gpus > 0
          and context.num_gpus() < number_of_required_gpus):
      return (False, ("Only {} of {} required GPUs are available.".format(
          context.num_gpus(), number_of_required_gpus)))
    else:
      return (True, None)

  def parameter_modifiers(self):
    return [combinations_lib.OptionalParameter("required_gpus")]


class TPUCombination(combinations_lib.TestCombination):
  """Allow to request TPU hardware and skip non-TPU combinations.

  This class expects test_combinations to be generated with `NamedDistribution`
  wrapping instances of `tf.distribute.Strategy`.

  Optionally, the `required_tpus` parameter is supported.  TPU hardware is
  required, if its argument is `True` or > 0.

  Optionally, the `use_cloud_tpu` parameter is supported. If TPU hardware is
  required by `required_tpus`, it specifically must be a Cloud TPU (specified
  with `--tpu`) if `use_cloud_tpu` is `True`.

  Attributes:
    TPU_TEST: The environment is considered to have TPU hardware available if
              the name of the program contains "test_tpu".
  """

  TPU_TEST = "test_tpu" in sys.argv[0]

  def should_execute_combination(self, kwargs):
    distributions = [
        v for v in kwargs.values() if isinstance(v, NamedDistribution)
    ]
    # TODO(isaprykin): Migrate all tests away from using 'required_tpu' in favor
    # of 'required_tpus'.
    if "required_tpus" in kwargs and "required_tpu" in kwargs:
      raise ValueError("Do not use `required_tpu`.  Both `required_tpus` and "
                       "`required_tpu` were specified.")
    required_tpus = kwargs.get("required_tpus", None) or kwargs.get(
        "required_tpu", None)

    if distributions and required_tpus:
      raise ValueError("Do not use `required_tpus` and arguments of type "
                       "NamedDistribution together.")

    # TODO(isaprykin): Add support for a particular number of TPUs.  Right now
    # it's binary.
    number_of_required_tpus = max([required_tpus or 0] +
                                  [d.required_tpu or 0 for d in distributions])
    use_cloud_tpu = any([kwargs.get("use_cloud_tpu")] +
                        [d.use_cloud_tpu for d in distributions])
    tpu = hasattr(flags.FLAGS, "tpu") and flags.FLAGS.tpu or ""

    if not number_of_required_tpus and TPUCombination.TPU_TEST:
      return (False, "Test that doesn't require TPUs.")
    if number_of_required_tpus and not TPUCombination.TPU_TEST:
      return (False, "Test requires a TPU, but it's not available.")
    if use_cloud_tpu and not tpu:
      return (False, "Test requires a Cloud TPU, but none specified.")
    if not use_cloud_tpu and tpu:
      return (False, "Test requires local TPU, but Cloud TPU specified.")
    return (True, None)

  def parameter_modifiers(self):
    return [
        combinations_lib.OptionalParameter("required_tpus"),
        combinations_lib.OptionalParameter("required_tpu"),
        combinations_lib.OptionalParameter("use_cloud_tpu"),
    ]


class NamedDistribution(object):
  """Wraps a `tf.distribute.Strategy` and adds a name for test titles."""

  def __init__(self,
               name,
               distribution_fn,
               required_gpus=None,
               required_tpu=False,
               use_cloud_tpu=False,
               has_chief=False,
               num_workers=1,
               use_pool_runner=False,
               no_xla=False):
    """Initialize NamedDistribution.

    Args:
      name: Name that will be a part of the name of the test case.
      distribution_fn: A callable that creates a `tf.distribute.Strategy`.
      required_gpus: The number of GPUs that the strategy requires.
      required_tpu: Whether the strategy requires TPU.
      use_cloud_tpu: Whether the strategy requires cloud TPU.
      has_chief: Whether the strategy requires a chief worker.
      num_workers: The number of workers that the strategy requires.
      use_pool_runner: Whether to use a pool runner so that workers are re-used
        each time.
      no_xla: Whether to skip in XLA tests.
    """
    object.__init__(self)
    self._name = name
    self._distribution_fn = distribution_fn
    self.required_gpus = required_gpus
    self.required_tpu = required_tpu
    self.use_cloud_tpu = use_cloud_tpu
    self.has_chief = has_chief
    self.num_workers = num_workers
    self.use_pool_runner = use_pool_runner
    self.no_xla = no_xla
    self._runner = None

  @property
  def runner(self):
    if not self._runner:
      if (_num_total_workers(self.has_chief, self.num_workers) > 1 and
          self.use_pool_runner):
        # Need to create the strategy in the initializer so that collectives are
        # configured before eager context initialization.
        cluster_spec = multi_worker_test_base.create_cluster_spec(
            has_chief=self.has_chief,
            num_workers=self.num_workers,
            num_ps=0,
            has_eval=False)
        self._runner = multi_process_runner.MultiProcessPoolRunner(
            cluster_spec, initializer=self._distribution_fn)
    return self._runner

  @property
  def strategy(self):
    return self._distribution_fn()

  def __repr__(self):
    return self._name


def concat(*combined):
  """Concats combinations."""
  result = []
  for one in combined:
    result += one
  return result


@tf_export("__internal__.distribute.combinations.generate", v1=[])
def generate(combinations, test_combinations=()):
  # pylint: disable=g-doc-args,g-doc-return-or-yield
  """Distributed adapter of `tf.__internal__.test.combinations.generate`.

  All tests with distributed strategy should use this one instead of
  `tf.__internal__.test.combinations.generate`. This function has support of
  strategy combinations, GPU/TPU and multi worker support.

  See `tf.__internal__.test.combinations.generate` for usage.
  """
  # pylint: enable=g-doc-args,g-doc-return-or-yield
  default_combinations = (
      framework_combinations.EagerGraphCombination(),
      framework_combinations.TFVersionCombination(),
      ClusterCombination(),
      DistributionCombination(),
      GPUCombination(),
      TPUCombination(),
  )
  # We apply our own decoration to handle multi worker tests before applying
  # framework.test_combinations.generate. The order is important since we need
  # framework.test_combinations.generate to apply all parameter modifiers first.
  combination_decorator = combinations_lib.generate(
      combinations, test_combinations=default_combinations + test_combinations)

  def decorator(test_method_or_class):
    if isinstance(test_method_or_class, type):
      # If it's a test class.
      class_object = test_method_or_class
      # Decorate each test method with _multi_worker_test.
      for name, test_method in six.iteritems(class_object.__dict__.copy()):
        if (name.startswith(unittest.TestLoader.testMethodPrefix) and
            isinstance(test_method, types.FunctionType)):
          setattr(class_object, name, _multi_worker_test(test_method))
      return combination_decorator(class_object)
    else:
      return combination_decorator(_multi_worker_test(test_method_or_class))

  return decorator


combine = combinations_lib.combine
times = combinations_lib.times
NamedObject = combinations_lib.NamedObject


# Identifies whether we're in the main process or worker processes.
# `_multi_worker_test` decoration behaves differently in the main processs and
# the worker processes. See the documentation of _multi_worker_test for detail.
_running_in_worker = False


_TestResult = collections.namedtuple("_TestResult", ["status", "message"])


def _test_runner(test_id):
  """Executes the test with the given test_id.

  This is a simple wrapper around TestRunner to be used with
  multi_process_runner. Similar to test.main(), but it executes only one test
  specified by test_id and returns whether the test succeeds. If the test fails,
  the function prints failures and errors to stdout.

  Args:
    test_id: TestCase.id()

  Returns:
    A boolean indicates whether the test succeeds.
  """
  global _running_in_worker
  # No need to restore the value of _running_in_worker since it should always be
  # True in worker processes.
  _running_in_worker = True
  test = unittest.defaultTestLoader.loadTestsFromName(test_id)
  runner = unittest.TextTestRunner()
  result = runner.run(test)
  # Treat expected failures as failures, so that the main process can get
  # them and fail as expected. Also treat errors as failures to simplify the
  # handling.
  failures = result.failures + result.expectedFailures + result.errors
  if failures:
    ret = _TestResult(status="failure", message=failures[0][1])
  elif result.skipped:
    ret = _TestResult(status="skipped", message=result.skipped[0][1])
  else:
    # Treat unexpectedSuccesses as OK so that the test case in the main process
    # succeed as well.
    ret = _TestResult(status="ok", message=None)
  # Print tracebacks to stdout and multi_process_runner will collect
  # them and stream back to the main process.
  if ret.message:
    print(ret.message)
  return ret


def _multi_worker_test(test_method):
  """Decorate test_method so that it runs in each worker.

  We use `multi_process_runner` to simulate multiple workers. Since we run the
  this function in the main process and all worker processes, this decoration
  behaves differently in the main process and worker procssses. In the main
  process, it spawns subprocesses and runs the test on each of them; in a worker
  process, it executes test in the same way as a normal test, e.g.
  setUp()/tearDown() are called before/after the test.

  Args:
    test_method: a function which must be a test method.

  Returns:
    Decorated `test_method`. Note that the decorated function has additional
    arguments.
  """

  def decorator(self, has_chief, num_workers, runner, **kwargs):
    if _num_total_workers(has_chief, num_workers) == 1 or _running_in_worker:
      # We're in worker process or the test is for single worker. Either case we
      # execute the test method directly instead of spawning subprocesses.

      # For MultiWorkerMirroredStrategy(CollectiveAllReduceStrategy), install a
      # session that connects to the local server. This is necessary for multi
      # worker graph mode tests to work. Those tests cannot use their graphs or
      # sessions, including the one returned by self.cached_session(). Since
      # existing tests may already be doing so, we only install the session for
      # multi worker tests.
      with _multi_worker_session(kwargs):
        test_method(self, **kwargs)
      return

    # We're in the main process. We spawn subprocesses and run the *test* on
    # each of them. Note that we're not directly executing test_method passed to
    # _multi_worker_test, because we need setUp()/tearDown() to be called and
    # all the decorations on the test method. The conceptual call stack is:
    #   [main process]test.main()
    #     [main process]test_runner.run(test)
    #       [main process]wrapper by combinations.generate()
    #         [main process]_multi_worker_test.decorator()
    #           # A sub process goes through the same code path as the main
    #           # process.
    #           [sub process]_test_runner()
    #             [sub process]test_runner.run(test)
    #               [sub process]wrapper by combinations.generate()
    #                 [sub process]_multi_worker_test.decorator()
    #                   # _running_in_worker is True
    #                   [sub process]test_method()
    test_id = self.id()
    if runner:
      results = runner.run(_test_runner, args=(test_id,))
    else:
      cluster_spec = multi_worker_test_base.create_cluster_spec(
          has_chief=has_chief,
          num_workers=num_workers,
          num_ps=0,
          has_eval=False)
      results = multi_process_runner.run(
          _test_runner, cluster_spec, args=(test_id,)).return_value

    skip_reason = None
    for result in results:
      if result.status == "failure":
        # We can't tell which worker the return value come from, so we fail on
        # the  first error.
        self.fail(result.message)
        break
      elif result.status == "skipped":
        # Record the skip reason, but do not actually skip the test in case some
        # processes fail instead.
        skip_reason = result.message
    if skip_reason is not None:
      self.skipTest(skip_reason)

  argspec = tf_inspect.getfullargspec(test_method)
  decorator_args = (argspec.args or []) + ["has_chief", "num_workers", "runner"]
  decorator_argspec = argspec._replace(args=decorator_args)
  return tf_decorator.make_decorator(
      test_method, decorator, decorator_argspec=decorator_argspec)


def _num_total_workers(has_chief, num_workers):
  """Returns the number of workers including the chief."""
  if has_chief:
    return num_workers + 1
  return num_workers


def _multi_worker_session(kwargs):
  """Returns a context manager that enters a session that is configured for the MultiWorkerMirroredStrategy.

  Args:
    kwargs: a dict. Keyword arguments passed to the test.

  Returns:
    A context manager. If MultiWorkerMirroredStrategy is the  one and only one
    strategy in kwargs and it's in graph mode, it's the seesion that is
    configured for that strategy.  Otherwise, it's a no-op context manager.
  """
  strategy = None
  for _, v in kwargs.items():
    if isinstance(v, distribute_lib.StrategyBase):
      if strategy is not None:
        logging.warning(
            "The test uses multiple strategies. Skipping "
            "entering a session that is configured for the strategy.")
        return ops.NullContextmanager()
      strategy = v
  if context.executing_eagerly() or not isinstance(
      strategy, collective_all_reduce_strategy.CollectiveAllReduceStrategy):
    return ops.NullContextmanager()
  sess_config = copy.deepcopy(context.context().config)
  sess_config = strategy.update_config_proto(sess_config)
  target = strategy.cluster_resolver.master()
  return session.Session(config=sess_config, target=target).as_default()
