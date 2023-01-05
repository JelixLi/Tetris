# Lint as: python3
# Copyright 2020 The TensorFlow Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ==============================================================================
"""Tests for ClusterCoordinator and Keras models."""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import random
import tempfile
from absl.testing import parameterized

from tensorflow.python import keras
from tensorflow.python.compat import v2_compat
from tensorflow.python.data.ops import dataset_ops
from tensorflow.python.distribute import combinations
from tensorflow.python.distribute import multi_worker_test_base
from tensorflow.python.distribute import parameter_server_strategy_v2
from tensorflow.python.distribute.cluster_resolver import SimpleClusterResolver
from tensorflow.python.distribute.coordinator import cluster_coordinator as coordinator_lib
from tensorflow.python.eager import backprop
from tensorflow.python.eager import def_function
from tensorflow.python.framework import constant_op
from tensorflow.python.framework import dtypes
from tensorflow.python.framework import tensor_spec
from tensorflow.python.keras.layers.preprocessing import string_lookup
from tensorflow.python.keras.optimizer_v2 import rmsprop
from tensorflow.python.ops import array_ops
from tensorflow.python.ops import math_ops
from tensorflow.python.ops import nn
from tensorflow.python.ops.losses import loss_reduction
from tensorflow.python.platform import test
from tensorflow.python.training.server_lib import ClusterSpec


# These vocabularies usually come from TFT or a Beam pipeline.
FEATURE_VOCAB = [
    "avenger", "ironman", "batman", "hulk", "spiderman", "kingkong",
    "wonder_woman"
]
LABEL_VOCAB = ["yes", "no"]


def make_coordinator(num_workers, num_ps):
  cluster_def = multi_worker_test_base.create_in_process_cluster(
      num_workers=num_workers, num_ps=num_ps, rpc_layer="grpc")
  cluster_def["chief"] = [
      "localhost:%d" % multi_worker_test_base.pick_unused_port()
  ]
  cluster_resolver = SimpleClusterResolver(
      ClusterSpec(cluster_def), rpc_layer="grpc")
  return coordinator_lib.ClusterCoordinator(
      parameter_server_strategy_v2.ParameterServerStrategyV2(cluster_resolver))


class KPLTest(test.TestCase, parameterized.TestCase):

  @classmethod
  def setUpClass(cls):
    super(KPLTest, cls).setUpClass()
    cls.coordinator = make_coordinator(num_workers=3, num_ps=2)

  def define_kpls_for_training(self, use_adapt):
    # Define KPLs under strategy's scope. Right now, if they have look up
    # tables, they will be created on the client. Their variables will be
    # created on PS. Ideally they should be cached on each worker since they
    # will not be changed in a training step.
    if use_adapt:
      feature_lookup_layer = string_lookup.StringLookup(num_oov_indices=1)
      feature_lookup_layer.adapt(FEATURE_VOCAB)
      label_lookup_layer = string_lookup.StringLookup(
          num_oov_indices=0, mask_token=None)
      label_lookup_layer.adapt(LABEL_VOCAB)
    else:
      feature_lookup_layer = string_lookup.StringLookup(
          vocabulary=FEATURE_VOCAB, num_oov_indices=1)
      label_lookup_layer = string_lookup.StringLookup(
          vocabulary=LABEL_VOCAB, num_oov_indices=0, mask_token=None)

    raw_feature_input = keras.layers.Input(
        shape=(3,), dtype=dtypes.string, name="feature", ragged=True)
    feature_id_input = feature_lookup_layer(raw_feature_input)

    # Model creates variables as well.
    feature_ps = keras.Model({"features": raw_feature_input}, feature_id_input)

    raw_label_input = keras.layers.Input(
        shape=(), dtype=dtypes.string, name="label")
    label_id_input = label_lookup_layer(raw_label_input)
    label_ps = keras.Model({"label": raw_label_input}, label_id_input)

    return feature_ps, label_ps

  def define_reverse_lookup_layer(self):
    # Only needed for serving.
    label_inverse_lookup_layer = string_lookup.StringLookup(
        num_oov_indices=1, mask_token=None, vocabulary=LABEL_VOCAB, invert=True)
    return label_inverse_lookup_layer

  @combinations.generate(
      combinations.combine(mode=["eager"], use_adapt=[True, False]))
  def testTrainAndServe(self, use_adapt):

    with self.coordinator.strategy.scope():

      feature_ps, label_ps = self.define_kpls_for_training(use_adapt)

      def dataset_fn():

        def feature_and_label_gen():
          while True:
            features = random.sample(FEATURE_VOCAB, 3)
            label = "yes" if "avenger" in features else "no"
            yield {"features": features, "label": label}

        # The dataset will be created on the coordinator?
        raw_dataset = dataset_ops.Dataset.from_generator(
            feature_and_label_gen,
            output_types={
                "features": dtypes.string,
                "label": dtypes.string
            }).shuffle(200).batch(32)
        preproc_dataset = raw_dataset.map(
            lambda x: {  # pylint: disable=g-long-lambda
                "features": feature_ps(x["features"]),
                "label": label_ps(x["label"])
            })
        train_dataset = preproc_dataset.map(lambda x: (  # pylint: disable=g-long-lambda
            {
                "features": x["features"]
            }, [x["label"]]))
        return train_dataset

      distributed_dataset = self.coordinator.create_per_worker_dataset(
          dataset_fn)

      # Create the model. The input needs to be compatible with KPLs.
      model_input = keras.layers.Input(
          shape=(3,), dtype=dtypes.int64, name="model_input")

      # input_dim includes a mask token and an oov token.
      emb_output = keras.layers.Embedding(
          input_dim=len(FEATURE_VOCAB) + 2, output_dim=20)(
              model_input)
      emb_output = math_ops.reduce_mean(emb_output, axis=1)
      dense_output = keras.layers.Dense(
          units=1, activation="sigmoid")(
              emb_output)
      model = keras.Model({"features": model_input}, dense_output)

      optimizer = rmsprop.RMSprop(learning_rate=0.01)
      accuracy = keras.metrics.Accuracy()

      @def_function.function
      def worker_fn(iterator):

        def replica_fn(iterator):
          batch_data, labels = next(iterator)
          with backprop.GradientTape() as tape:
            pred = model(batch_data, training=True)
            loss = nn.compute_average_loss(
                keras.losses.BinaryCrossentropy(
                    reduction=loss_reduction.ReductionV2.NONE)(labels, pred))
            gradients = tape.gradient(loss, model.trainable_variables)

          optimizer.apply_gradients(zip(gradients, model.trainable_variables))

          actual_pred = math_ops.cast(math_ops.greater(pred, 0.5), dtypes.int64)
          accuracy.update_state(labels, actual_pred)

        self.coordinator._strategy.run(replica_fn, args=(iterator,))

    distributed_iterator = iter(distributed_dataset)
    for _ in range(10):
      self.coordinator.schedule(worker_fn, args=(distributed_iterator,))
    self.coordinator.join()
    self.assertGreater(accuracy.result().numpy(), 0.0)

    # Create a saved model.
    model.feature_ps = feature_ps
    model.label_ps = label_ps
    model.label_inverse_lookup_layer = self.define_reverse_lookup_layer()

    def create_serving_signature(model):

      @def_function.function
      def serve_fn(raw_features):
        raw_features = array_ops.expand_dims(raw_features, axis=0)
        transformed_features = model.feature_ps(raw_features)
        outputs = model(transformed_features)
        outputs = array_ops.squeeze(outputs, axis=0)
        outputs = math_ops.cast(math_ops.greater(outputs, 0.5), dtypes.int64)
        decoded_outputs = model.label_inverse_lookup_layer(outputs)
        return array_ops.squeeze(decoded_outputs, axis=0)

      # serving does NOT have batch dimension
      return serve_fn.get_concrete_function(
          tensor_spec.TensorSpec(
              shape=(3), dtype=dtypes.string, name="example"))

    serving_fn = create_serving_signature(model)

    saved_model_dir = tempfile.mkdtemp(dir=self.get_temp_dir())
    model.save(saved_model_dir, signatures={"serving_default": serving_fn})

    # Test the saved_model.
    loaded_serving_fn = keras.saving.save.load_model(
        saved_model_dir).signatures["serving_default"]

    # check the result w/ and w/o avenger.
    prediction0 = loaded_serving_fn(
        constant_op.constant(["avenger", "ironman", "avenger"]))["output_0"]
    self.assertIn(prediction0, ("yes", "no"))

    prediction1 = loaded_serving_fn(
        constant_op.constant(["ironman", "ironman", "unkonwn"]))["output_0"]
    self.assertIn(prediction1, ("yes", "no"))


if __name__ == "__main__":
  v2_compat.enable_v2_behavior()
  test.main()
