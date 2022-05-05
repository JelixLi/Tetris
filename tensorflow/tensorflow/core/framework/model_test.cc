/* Copyright 2018 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/core/framework/model.h"
#include <memory>

#include "tensorflow/core/lib/gtl/cleanup.h"
#include "tensorflow/core/platform/test.h"

namespace tensorflow {
namespace data {
namespace model {
namespace {

class AsyncInterleaveManyTest
    : public ::testing::TestWithParam<std::tuple<int64, double>> {};

TEST_P(AsyncInterleaveManyTest, Model) {
  const int64 parallelism = std::get<0>(GetParam());
  const double input_time = std::get<1>(GetParam());
  std::shared_ptr<Node> async_interleave_many =
      model::MakeAsyncInterleaveManyNode(
          {0, "async_interleave_many", nullptr},
          {model::MakeParameter(
              "parallelism",
              std::make_shared<SharedState>(parallelism, nullptr, nullptr), 1,
              parallelism)});
  std::shared_ptr<Node> meta_source =
      model::MakeSourceNode({1, "meta_source", async_interleave_many});
  async_interleave_many->add_input(meta_source);
  auto cleanup_meta = gtl::MakeCleanup([async_interleave_many, meta_source]() {
    async_interleave_many->remove_input(meta_source);
  });
  std::shared_ptr<Node> source1 =
      model::MakeSourceNode({2, "source1", async_interleave_many});
  async_interleave_many->add_input(source1);
  auto cleanup1 = gtl::MakeCleanup([async_interleave_many, source1]() {
    async_interleave_many->remove_input(source1);
  });
  std::shared_ptr<Node> source2 =
      model::MakeSourceNode({3, "source2", async_interleave_many});
  async_interleave_many->add_input(source2);
  auto cleanup2 = gtl::MakeCleanup([async_interleave_many, source2]() {
    async_interleave_many->remove_input(source2);
  });
  absl::flat_hash_map<string, double> input_times;
  input_times[kModelInputTimeKey] = input_time;
  EXPECT_EQ(async_interleave_many->TotalBufferedBytes(), 0);
  EXPECT_EQ(async_interleave_many->TotalMaximumBufferedBytes(), 0);
  async_interleave_many->record_buffer_event(110, 10);
  EXPECT_EQ(async_interleave_many->TotalBufferedBytes(), 110);
  EXPECT_EQ(async_interleave_many->TotalMaximumBufferedBytes(),
            110 * parallelism / 10);
  async_interleave_many->add_processing_time(100);
  EXPECT_EQ(async_interleave_many->processing_time(), 100);
  EXPECT_EQ(
      async_interleave_many->TotalProcessingTime(/*processing_times=*/nullptr),
      0);
  EXPECT_EQ(async_interleave_many->OutputTime(&input_times, nullptr), 0);
  async_interleave_many->record_element();
  EXPECT_EQ(async_interleave_many->num_elements(), 1);
  EXPECT_EQ(
      async_interleave_many->TotalProcessingTime(/*processing_times=*/nullptr),
      100);
  EXPECT_LE(async_interleave_many->OutputTime(&input_times, nullptr), 100);
  EXPECT_GE(async_interleave_many->OutputTime(&input_times, nullptr), 0);
  source1->add_processing_time(200);
  source2->add_processing_time(300);
  EXPECT_EQ(
      async_interleave_many->TotalProcessingTime(/*processing_times=*/nullptr),
      100);
  EXPECT_LE(async_interleave_many->OutputTime(&input_times, nullptr), 100);
  EXPECT_GE(async_interleave_many->OutputTime(&input_times, nullptr), 0);
  source1->record_element();
  source2->record_element();
  EXPECT_EQ(
      async_interleave_many->TotalProcessingTime(/*processing_times=*/nullptr),
      100 + 250);
  EXPECT_LE(async_interleave_many->OutputTime(&input_times, nullptr),
            100 + 250 / parallelism);
  EXPECT_GE(async_interleave_many->OutputTime(&input_times, nullptr), 0);
  async_interleave_many->record_element();
  EXPECT_EQ(
      async_interleave_many->TotalProcessingTime(/*processing_times=*/nullptr),
      50 + 250);
  EXPECT_LE(async_interleave_many->OutputTime(&input_times, nullptr),
            50 + 250 / parallelism);
  EXPECT_GE(async_interleave_many->OutputTime(&input_times, nullptr), 0);
}

INSTANTIATE_TEST_SUITE_P(Test, AsyncInterleaveManyTest,
                         ::testing::Combine(::testing::Values(1, 2),
                                            ::testing::Values(0, 50, 100,
                                                              200)));

class AsyncKnownRatioTest
    : public ::testing::TestWithParam<std::tuple<int64, double, int64>> {};

TEST_P(AsyncKnownRatioTest, Model) {
  const int64 parallelism = std::get<0>(GetParam());
  const double input_time = std::get<1>(GetParam());
  const int64 num_inputs_per_output = std::get<2>(GetParam());
  std::shared_ptr<Node> async_known_many = model::MakeAsyncKnownRatioNode(
      {0, "async_known_many", nullptr}, num_inputs_per_output,
      {model::MakeParameter(
          "parallelism",
          std::make_shared<SharedState>(parallelism, nullptr, nullptr), 1,
          parallelism)});
  std::shared_ptr<Node> source1 =
      model::MakeSourceNode({1, "source1", async_known_many});
  async_known_many->add_input(source1);
  std::shared_ptr<Node> source2 =
      model::MakeSourceNode({2, "source2", async_known_many});
  async_known_many->add_input(source2);
  absl::flat_hash_map<string, double> input_times;
  input_times[kModelInputTimeKey] = input_time;
  EXPECT_EQ(async_known_many->TotalBufferedBytes(), 0);
  EXPECT_EQ(async_known_many->TotalMaximumBufferedBytes(), 0);
  async_known_many->record_buffer_event(110, 10);
  EXPECT_EQ(async_known_many->TotalBufferedBytes(), 110);
  EXPECT_EQ(async_known_many->TotalMaximumBufferedBytes(),
            num_inputs_per_output == 0
                ? 110.0 * parallelism / 10
                : 110.0 * parallelism / 10 / num_inputs_per_output);
  source1->add_processing_time(100);
  EXPECT_EQ(async_known_many->TotalProcessingTime(/*processing_times=*/nullptr),
            0);
  EXPECT_EQ(async_known_many->OutputTime(&input_times, nullptr), 0);
  source2->add_processing_time(200);
  EXPECT_EQ(async_known_many->TotalProcessingTime(/*processing_times=*/nullptr),
            0);
  EXPECT_EQ(async_known_many->OutputTime(&input_times, nullptr), 0);
  source1->record_element();
  EXPECT_EQ(async_known_many->TotalProcessingTime(/*processing_times=*/nullptr),
            num_inputs_per_output * 100);
  EXPECT_LE(async_known_many->OutputTime(&input_times, nullptr),
            num_inputs_per_output * 100);
  EXPECT_GE(async_known_many->OutputTime(&input_times, nullptr), 0);
  source2->record_element();
  EXPECT_EQ(async_known_many->TotalProcessingTime(/*processing_times=*/nullptr),
            num_inputs_per_output * (100 + 200));
  EXPECT_LE(async_known_many->OutputTime(&input_times, nullptr),
            num_inputs_per_output * (100 + 200));
  EXPECT_GE(async_known_many->OutputTime(&input_times, nullptr), 0);
  source1->record_element();
  EXPECT_EQ(async_known_many->TotalProcessingTime(/*processing_times=*/nullptr),
            num_inputs_per_output * (50 + 200));
  EXPECT_LE(async_known_many->OutputTime(&input_times, nullptr),
            num_inputs_per_output * (50 + 200));
  EXPECT_GE(async_known_many->OutputTime(&input_times, nullptr), 0);
  source2->record_element();
  EXPECT_EQ(async_known_many->TotalProcessingTime(/*processing_times=*/nullptr),
            num_inputs_per_output * (50 + 100));
  EXPECT_LE(async_known_many->OutputTime(&input_times, nullptr),
            num_inputs_per_output * (50 + 100));
  EXPECT_GE(async_known_many->OutputTime(&input_times, nullptr), 0);
  async_known_many->add_processing_time(128);
  EXPECT_EQ(async_known_many->TotalProcessingTime(/*processing_times=*/nullptr),
            num_inputs_per_output * (50 + 100));
  EXPECT_LE(async_known_many->OutputTime(&input_times, nullptr),
            num_inputs_per_output * (50 + 100));
  EXPECT_GE(async_known_many->OutputTime(&input_times, nullptr), 0);
  async_known_many->record_element();
  EXPECT_EQ(async_known_many->TotalProcessingTime(/*processing_times=*/nullptr),
            num_inputs_per_output * (50 + 100) + 128);
  EXPECT_LE(async_known_many->OutputTime(&input_times, nullptr),
            num_inputs_per_output * (50 + 100) + 128 / parallelism);
  EXPECT_GE(async_known_many->OutputTime(&input_times, nullptr), 0);
  async_known_many->record_element();
  EXPECT_EQ(async_known_many->TotalProcessingTime(/*processing_times=*/nullptr),
            num_inputs_per_output * (50 + 100) + 64);
  EXPECT_LE(async_known_many->OutputTime(&input_times, nullptr),
            num_inputs_per_output * (50 + 100) + 64 / parallelism);
  EXPECT_GE(async_known_many->OutputTime(&input_times, nullptr), 0);
}

INSTANTIATE_TEST_SUITE_P(Test, AsyncKnownRatioTest,
                         ::testing::Combine(::testing::Values(1, 2, 4, 8),
                                            ::testing::Values(0, 50, 100, 200),
                                            ::testing::Values(0, 1, 2, 4)));

TEST(InterleaveManyTest, Model) {
  std::shared_ptr<Node> interleave_many =
      model::MakeInterleaveManyNode({0, "interleave_many", nullptr});
  std::shared_ptr<Node> meta_source =
      model::MakeSourceNode({1, "meta_source", interleave_many});
  interleave_many->add_input(meta_source);
  std::shared_ptr<Node> source1 =
      model::MakeSourceNode({2, "source1", interleave_many});
  interleave_many->add_input(source1);
  std::shared_ptr<Node> source2 =
      model::MakeSourceNode({3, "source2", interleave_many});
  interleave_many->add_input(source2);
  absl::flat_hash_map<string, double> input_times;
  input_times[kModelInputTimeKey] = 0.0;
  interleave_many->add_processing_time(100);
  EXPECT_EQ(interleave_many->processing_time(), 100);
  EXPECT_EQ(interleave_many->TotalProcessingTime(/*processing_times=*/nullptr),
            0);
  EXPECT_EQ(interleave_many->OutputTime(&input_times, nullptr), 0);
  interleave_many->record_element();
  EXPECT_EQ(interleave_many->num_elements(), 1);
  EXPECT_EQ(interleave_many->TotalProcessingTime(/*processing_times=*/nullptr),
            100);
  EXPECT_EQ(interleave_many->OutputTime(&input_times, nullptr), 100);
  source1->add_processing_time(200);
  source2->add_processing_time(300);
  EXPECT_EQ(interleave_many->TotalProcessingTime(/*processing_times=*/nullptr),
            100);
  EXPECT_EQ(interleave_many->OutputTime(&input_times, nullptr), 100);
  source1->record_element();
  source2->record_element();
  EXPECT_EQ(interleave_many->TotalProcessingTime(/*processing_times=*/nullptr),
            350);
  EXPECT_EQ(interleave_many->OutputTime(&input_times, nullptr), 350);
  interleave_many->record_element();
  EXPECT_EQ(interleave_many->TotalProcessingTime(/*processing_times=*/nullptr),
            300);
  EXPECT_EQ(interleave_many->OutputTime(&input_times, nullptr), 300);
}

class KnownRatioTest : public ::testing::TestWithParam<int64> {};

TEST_P(KnownRatioTest, Model) {
  const int64 num_inputs_per_output = GetParam();
  std::shared_ptr<Node> known_many = model::MakeKnownRatioNode(
      {0, "known_many", nullptr}, num_inputs_per_output);
  std::shared_ptr<Node> source1 =
      model::MakeSourceNode({1, "source1", known_many});
  known_many->add_input(source1);
  std::shared_ptr<Node> source2 =
      model::MakeSourceNode({2, "source2", known_many});
  known_many->add_input(source2);
  absl::flat_hash_map<string, double> input_times;
  input_times[kModelInputTimeKey] = 0.0;
  source1->add_processing_time(100);
  EXPECT_EQ(known_many->TotalProcessingTime(/*processing_times=*/nullptr), 0);
  EXPECT_EQ(known_many->OutputTime(&input_times, nullptr), 0);
  source2->add_processing_time(200);
  EXPECT_EQ(known_many->TotalProcessingTime(/*processing_times=*/nullptr), 0);
  EXPECT_EQ(known_many->OutputTime(&input_times, nullptr), 0);
  source1->record_element();
  EXPECT_EQ(known_many->TotalProcessingTime(/*processing_times=*/nullptr),
            num_inputs_per_output * 100);
  EXPECT_EQ(known_many->OutputTime(&input_times, nullptr),
            num_inputs_per_output * 100);
  source2->record_element();
  EXPECT_EQ(known_many->TotalProcessingTime(/*processing_times=*/nullptr),
            num_inputs_per_output * (100 + 200));
  EXPECT_EQ(known_many->OutputTime(&input_times, nullptr),
            num_inputs_per_output * (100 + 200));
  source1->record_element();
  EXPECT_EQ(known_many->TotalProcessingTime(/*processing_times=*/nullptr),
            num_inputs_per_output * (50 + 200));
  EXPECT_EQ(known_many->OutputTime(&input_times, nullptr),
            num_inputs_per_output * (50 + 200));
  source2->record_element();
  EXPECT_EQ(known_many->TotalProcessingTime(/*processing_times=*/nullptr),
            num_inputs_per_output * (50 + 100));
  EXPECT_EQ(known_many->OutputTime(&input_times, nullptr),
            num_inputs_per_output * (50 + 100));
  known_many->add_processing_time(128);
  EXPECT_EQ(known_many->TotalProcessingTime(/*processing_times=*/nullptr),
            num_inputs_per_output * (50 + 100));
  EXPECT_EQ(known_many->OutputTime(&input_times, nullptr),
            num_inputs_per_output * (50 + 100));
  known_many->record_element();
  EXPECT_EQ(known_many->TotalProcessingTime(/*processing_times=*/nullptr),
            num_inputs_per_output * (50 + 100) + 128);
  EXPECT_EQ(known_many->OutputTime(&input_times, nullptr),
            num_inputs_per_output * (50 + 100) + 128);
  known_many->record_element();
  EXPECT_EQ(known_many->TotalProcessingTime(/*processing_times=*/nullptr),
            num_inputs_per_output * (50 + 100) + 64);
  EXPECT_EQ(known_many->OutputTime(&input_times, nullptr),
            num_inputs_per_output * (50 + 100) + 64);
}

INSTANTIATE_TEST_SUITE_P(Test, KnownRatioTest, ::testing::Values(0, 1, 2, 4));

TEST(SourceTest, Model) {
  std::shared_ptr<Node> source = model::MakeSourceNode({0, "source", nullptr});
  absl::flat_hash_map<string, double> input_times;
  input_times[kModelInputTimeKey] = 0.0;
  source->add_processing_time(100);
  EXPECT_EQ(source->processing_time(), 100);
  EXPECT_EQ(source->TotalProcessingTime(/*processing_times=*/nullptr), 0);
  EXPECT_EQ(source->OutputTime(&input_times, nullptr), 0);
  source->record_element();
  EXPECT_EQ(source->num_elements(), 1);
  EXPECT_EQ(source->TotalProcessingTime(/*processing_times=*/nullptr), 100);
  EXPECT_EQ(source->OutputTime(&input_times, nullptr), 100);
  source->record_element();
  EXPECT_EQ(source->num_elements(), 2);
  EXPECT_EQ(source->TotalProcessingTime(/*processing_times=*/nullptr), 50);
  EXPECT_EQ(source->OutputTime(&input_times, nullptr), 50);
}

TEST(UnknownRatioTest, Model) {
  std::shared_ptr<Node> unknown_many =
      model::MakeUnknownRatioNode({0, "unknown_many", nullptr});
  std::shared_ptr<Node> source1 =
      model::MakeSourceNode({1, "source1", unknown_many});
  unknown_many->add_input(source1);
  std::shared_ptr<Node> source2 =
      model::MakeSourceNode({2, "source2", unknown_many});
  unknown_many->add_input(source2);
  absl::flat_hash_map<string, double> input_times;
  input_times[kModelInputTimeKey] = 0.0;
  unknown_many->add_processing_time(100);
  EXPECT_EQ(unknown_many->processing_time(), 100);
  EXPECT_EQ(unknown_many->TotalProcessingTime(/*processing_times=*/nullptr), 0);
  EXPECT_EQ(unknown_many->OutputTime(&input_times, nullptr), 0);
  unknown_many->record_element();
  EXPECT_EQ(unknown_many->num_elements(), 1);
  EXPECT_EQ(unknown_many->TotalProcessingTime(/*processing_times=*/nullptr),
            100);
  EXPECT_EQ(unknown_many->OutputTime(&input_times, nullptr), 100);
  source1->add_processing_time(100);
  source2->add_processing_time(200);
  EXPECT_EQ(unknown_many->TotalProcessingTime(/*processing_times=*/nullptr),
            100);
  EXPECT_EQ(unknown_many->OutputTime(&input_times, nullptr), 100);
  source1->record_element();
  source2->record_element();
  EXPECT_EQ(unknown_many->TotalProcessingTime(/*processing_times=*/nullptr),
            400);
  EXPECT_EQ(unknown_many->OutputTime(&input_times, nullptr), 400);
  unknown_many->record_element();
  EXPECT_EQ(unknown_many->TotalProcessingTime(/*processing_times=*/nullptr),
            200);
  EXPECT_EQ(unknown_many->OutputTime(&input_times, nullptr), 200);
}

TEST(UnknownTest, Model) {
  std::shared_ptr<Node> unknown =
      model::MakeUnknownNode({0, "unknown", nullptr});
  std::shared_ptr<Node> source1 =
      model::MakeSourceNode({1, "source1", unknown});
  unknown->add_input(source1);
  std::shared_ptr<Node> source2 =
      model::MakeSourceNode({2, "source2", unknown});
  unknown->add_input(source2);
  absl::flat_hash_map<string, double> input_times;
  input_times[kModelInputTimeKey] = 0.0;
  source1->add_processing_time(100);
  EXPECT_EQ(unknown->TotalProcessingTime(/*processing_times=*/nullptr), 0);
  EXPECT_EQ(unknown->OutputTime(&input_times, nullptr), 0);
  source2->add_processing_time(100);
  EXPECT_EQ(unknown->TotalProcessingTime(/*processing_times=*/nullptr), 0);
  EXPECT_EQ(unknown->OutputTime(&input_times, nullptr), 0);
  source1->record_element();
  EXPECT_EQ(unknown->TotalProcessingTime(/*processing_times=*/nullptr), 100);
  EXPECT_EQ(unknown->OutputTime(&input_times, nullptr), 100);
  source2->record_element();
  EXPECT_EQ(unknown->TotalProcessingTime(/*processing_times=*/nullptr), 200);
  EXPECT_EQ(unknown->OutputTime(&input_times, nullptr), 200);
  source1->record_element();
  EXPECT_EQ(unknown->TotalProcessingTime(/*processing_times=*/nullptr), 150);
  EXPECT_EQ(unknown->OutputTime(&input_times, nullptr), 150);
  source2->record_element();
  EXPECT_EQ(unknown->TotalProcessingTime(/*processing_times=*/nullptr), 100);
  EXPECT_EQ(unknown->OutputTime(&input_times, nullptr), 100);
  // Unknown node processing time should not affect its TotalProcessingTime() or
  // OutputTime().
  unknown->add_processing_time(100);
  EXPECT_EQ(unknown->processing_time(), 100);
  EXPECT_EQ(unknown->TotalProcessingTime(/*processing_times=*/nullptr), 100);
  EXPECT_EQ(unknown->OutputTime(&input_times, nullptr), 100);
  // Unknown node number of elements should not affect its TotalProcessingTime()
  // or OutputTime().
  unknown->record_element();
  EXPECT_EQ(unknown->num_elements(), 1);
  EXPECT_EQ(unknown->TotalProcessingTime(/*processing_times=*/nullptr), 100);
  EXPECT_EQ(unknown->OutputTime(&input_times, nullptr), 100);
}

TEST(BufferedBytesTest, Node) {
  std::shared_ptr<Node> node = model::MakeAsyncInterleaveManyNode(
      {-1, "TestNode", nullptr},
      {model::MakeParameter("parallelism",
                            std::make_shared<SharedState>(3, nullptr, nullptr),
                            1, 7)});
  EXPECT_EQ(node->id(), -1);
  EXPECT_EQ(node->name(), "TestNode");
  EXPECT_EQ(node->output(), nullptr);

  EXPECT_EQ(node->buffered_bytes(), 0);
  EXPECT_EQ(node->buffered_elements(), 0);
  EXPECT_EQ(node->TotalBufferedBytes(), 0);
  EXPECT_EQ(node->TotalMaximumBufferedBytes(), 0);

  node->record_buffer_event(20, 1);
  EXPECT_EQ(node->buffered_bytes(), 20);
  EXPECT_EQ(node->buffered_elements(), 1);
  EXPECT_EQ(node->TotalBufferedBytes(), 20);
  EXPECT_EQ(node->TotalMaximumBufferedBytes(), 60);

  node->record_buffer_event(10, 1);
  EXPECT_EQ(node->buffered_bytes(), 30);
  EXPECT_EQ(node->buffered_elements(), 2);
  EXPECT_EQ(node->TotalBufferedBytes(), 30);
  EXPECT_EQ(node->TotalMaximumBufferedBytes(), 45);

  node->record_buffer_event(18, 1);
  EXPECT_EQ(node->buffered_bytes(), 48);
  EXPECT_EQ(node->buffered_elements(), 3);
  EXPECT_EQ(node->bytes_produced(), 0);
  EXPECT_EQ(node->num_elements(), 0);
  EXPECT_EQ(node->TotalBufferedBytes(), 48);
  EXPECT_EQ(node->TotalMaximumBufferedBytes(), 48);

  node->record_buffer_event(-20, -1);
  node->record_element();
  node->record_bytes_produced(20);
  EXPECT_EQ(node->buffered_bytes(), 28);
  EXPECT_EQ(node->buffered_elements(), 2);
  EXPECT_EQ(node->bytes_produced(), 20);
  EXPECT_EQ(node->num_elements(), 1);
  EXPECT_EQ(node->TotalBufferedBytes(), 28);
  EXPECT_EQ(node->TotalMaximumBufferedBytes(), 51);

  node->record_buffer_event(-10, -1);
  node->record_element();
  node->record_bytes_produced(10);
  EXPECT_EQ(node->buffered_bytes(), 18);
  EXPECT_EQ(node->buffered_elements(), 1);
  EXPECT_EQ(node->bytes_produced(), 30);
  EXPECT_EQ(node->num_elements(), 2);
  EXPECT_EQ(node->TotalBufferedBytes(), 18);
  EXPECT_EQ(node->TotalMaximumBufferedBytes(), 49.5);

  EXPECT_EQ(node->processing_time(), 0);
  node->record_start(1);
  EXPECT_EQ(node->processing_time(), 0);
  node->record_stop(41);
  EXPECT_EQ(node->processing_time(), 40);
  node->add_processing_time(2);
  EXPECT_EQ(node->processing_time(), 42);

  std::shared_ptr<Node> input = model::MakeAsyncKnownRatioNode(
      {0, "TestInput", node}, 2,
      {model::MakeParameter("parallelism",
                            std::make_shared<SharedState>(5, nullptr, nullptr),
                            0, 6)});
  EXPECT_EQ(input->output(), node.get());
  EXPECT_EQ(node->inputs().size(), 0);
  node->add_input(input);
  EXPECT_EQ(node->inputs().size(), 1);
  EXPECT_EQ(node->inputs().front(), input);

  input->record_buffer_event(28, 1);
  EXPECT_EQ(node->bytes_consumed(), 0);
  EXPECT_EQ(node->TotalBufferedBytes(), 46);
  EXPECT_EQ(node->TotalMaximumBufferedBytes(), 119.5);

  input->record_buffer_event(-28, -1);
  input->record_element();
  input->record_bytes_produced(28);
  node->record_bytes_consumed(28);
  EXPECT_EQ(node->bytes_consumed(), 28);
  EXPECT_EQ(node->TotalBufferedBytes(), 18);
  EXPECT_EQ(node->TotalMaximumBufferedBytes(), 119.5);

  node->remove_input(input);
  EXPECT_EQ(node->inputs().size(), 0);
}

// Returns a weighted sum of a prior and the actual processing time.
double weighted_processing_time(int64 num_elements, double processing_time,
                                double prior) {
  if (num_elements < 30) {
    double prior_weight = 1.0L / static_cast<double>(2 << num_elements);
    return prior_weight * prior + (1.0L - prior_weight) * processing_time;
  } else {
    return processing_time;
  }
}

TEST(TestManyElements, Model) {
  std::shared_ptr<Node> interleave_many =
      model::MakeInterleaveManyNode({0, "interleave_many", nullptr});
  std::shared_ptr<Node> source1 =
      model::MakeSourceNode({1, "source1", interleave_many});
  interleave_many->add_input(source1);
  interleave_many->add_processing_time(100);
  interleave_many->record_element();
  source1->add_processing_time(200);
  for (int i = 0; i < 100; i++) {
    source1->record_element();
  }
  EXPECT_LE(interleave_many->TotalProcessingTime(/*processing_times=*/nullptr),
            (weighted_processing_time(100, 2, 0)) + 100);
  EXPECT_GE(interleave_many->TotalProcessingTime(/*processing_times=*/nullptr),
            0);
}

// Precision for comparison of the gradient and a relative output time change.
constexpr double kComparisonPrecision = 1e-1;

// Parameter step for a relative output time change.
constexpr double kParameterStep = 1e-5;

TEST(AsyncInterleaveManyGradientTest, Model) {
  const int64 parallelism = model::kAutotune;
  const double input_time = 100;
  std::shared_ptr<Node> async_interleave_many =
      model::MakeAsyncInterleaveManyNode(
          {0, "async_interleave_many", nullptr},
          {model::MakeParameter(
              "parallelism",
              std::make_shared<SharedState>(parallelism, nullptr, nullptr), 1,
              parallelism)});
  std::shared_ptr<Node> meta_source =
      model::MakeSourceNode({1, "meta_source", async_interleave_many});
  async_interleave_many->add_input(meta_source);
  auto cleanup_meta = gtl::MakeCleanup([async_interleave_many, meta_source]() {
    async_interleave_many->remove_input(meta_source);
  });
  std::shared_ptr<Node> source1 = model::MakeAsyncInterleaveManyNode(
      {2, "async_interleave_many", async_interleave_many},
      {model::MakeParameter(
          "parallelism",
          std::make_shared<SharedState>(parallelism, nullptr, nullptr), 1,
          parallelism)});
  async_interleave_many->add_input(source1);
  auto cleanup1 = gtl::MakeCleanup([async_interleave_many, source1]() {
    async_interleave_many->remove_input(source1);
  });
  std::shared_ptr<Node> source2 =
      model::MakeSourceNode({3, "source2", async_interleave_many});
  async_interleave_many->add_input(source2);
  auto cleanup2 = gtl::MakeCleanup([async_interleave_many, source2]() {
    async_interleave_many->remove_input(source2);
  });
  absl::flat_hash_map<string, double> input_times;
  input_times[kModelInputTimeKey] = input_time;
  absl::flat_hash_map<string, std::shared_ptr<Parameter>> parameters;
  async_interleave_many->CollectTunableParameters(&parameters);
  async_interleave_many->record_element();
  async_interleave_many->add_processing_time(100);
  source1->record_element();
  source1->add_processing_time(100);
  source2->record_element();
  source2->add_processing_time(300);
  parameters[async_interleave_many->long_name()]->value = 1;
  parameters[source1->long_name()]->value = 1;

  // Test gradient of own parameters.
  absl::flat_hash_map<string, double> gradients;
  double output_time =
      async_interleave_many->OutputTime(&input_times, &gradients);
  parameters[async_interleave_many->long_name()]->value += kParameterStep;
  double new_output_time =
      async_interleave_many->OutputTime(&input_times, nullptr);
  EXPECT_NEAR(gradients[async_interleave_many->long_name()],
              (new_output_time - output_time) / kParameterStep,
              kComparisonPrecision);

  // Test propagation of input's gradient.
  parameters[async_interleave_many->long_name()]->value -= kParameterStep;
  parameters[source1->long_name()]->value += kParameterStep;
  new_output_time = async_interleave_many->OutputTime(&input_times, nullptr);
  EXPECT_NEAR(gradients[source1->long_name()],
              (new_output_time - output_time) / kParameterStep,
              kComparisonPrecision);
}

class AsyncKnownRatioGradientTest : public ::testing::TestWithParam<string> {};

TEST_P(AsyncKnownRatioGradientTest, Model) {
  const string parameter_name = GetParam();
  const int64 parameter_value = model::kAutotune;
  const double input_time = 100;
  const int64 num_inputs_per_output = 2;
  std::shared_ptr<Node> async_known_many = model::MakeAsyncKnownRatioNode(
      {0, "async_known_many", nullptr}, num_inputs_per_output,
      {model::MakeParameter(
          parameter_name,
          std::make_shared<SharedState>(parameter_value, nullptr, nullptr), 1,
          parameter_value)});
  std::shared_ptr<Node> source1 = model::MakeAsyncKnownRatioNode(
      {1, "source1", async_known_many}, num_inputs_per_output,
      {model::MakeParameter(
          parameter_name,
          std::make_shared<SharedState>(parameter_value, nullptr, nullptr), 1,
          parameter_value)});
  async_known_many->add_input(source1);
  std::shared_ptr<Node> source2 =
      model::MakeSourceNode({2, "source2", async_known_many});
  absl::flat_hash_map<string, double> input_times;
  input_times[kModelInputTimeKey] = input_time;
  async_known_many->add_input(source2);
  source1->record_element();
  source1->add_processing_time(100);
  source2->record_element();
  source2->add_processing_time(100);
  async_known_many->record_element();
  async_known_many->add_processing_time(300);

  // Test gradient of own parameters.
  absl::flat_hash_map<string, std::shared_ptr<Parameter>> parameters;
  absl::flat_hash_map<string, double> gradients;
  async_known_many->CollectTunableParameters(&parameters);
  parameters[async_known_many->long_name()]->value = 1;
  parameters[source1->long_name()]->value = 1;
  double output_time = async_known_many->OutputTime(&input_times, &gradients);
  parameters[async_known_many->long_name()]->value += kParameterStep;
  double new_output_time = async_known_many->OutputTime(&input_times, nullptr);
  EXPECT_NEAR(gradients[async_known_many->long_name()],
              (new_output_time - output_time) / kParameterStep,
              kComparisonPrecision);

  // Test propagation of input's gradient.
  parameters[async_known_many->long_name()]->value -= kParameterStep;
  parameters[source1->long_name()]->value += kParameterStep;
  new_output_time = async_known_many->OutputTime(&input_times, nullptr);
  EXPECT_NEAR(gradients[source1->long_name()],
              (new_output_time - output_time) / kParameterStep,
              kComparisonPrecision);
}

INSTANTIATE_TEST_SUITE_P(Test, AsyncKnownRatioGradientTest,
                         ::testing::Values("parallelism", "buffer_size"));

TEST(InterleaveManyGradientTest, Model) {
  const int64 parallelism = model::kAutotune;
  const double input_time = 100;
  const int64 num_inputs_per_output = 2;
  std::shared_ptr<Node> interleave_many =
      model::MakeInterleaveManyNode({0, "interleave_many", nullptr});
  std::shared_ptr<Node> async_known_many = model::MakeAsyncKnownRatioNode(
      {1, "async_known_many", interleave_many}, num_inputs_per_output,
      {model::MakeParameter(
          "parallelism",
          std::make_shared<SharedState>(parallelism, nullptr, nullptr), 1,
          parallelism)});
  std::shared_ptr<Node> source1 =
      model::MakeSourceNode({2, "source1", interleave_many});
  interleave_many->record_element();
  interleave_many->add_processing_time(100);
  interleave_many->add_input(source1);
  interleave_many->add_input(async_known_many);
  async_known_many->record_element();
  async_known_many->add_processing_time(300);
  absl::flat_hash_map<string, double> input_times;
  input_times[kModelInputTimeKey] = input_time;
  absl::flat_hash_map<string, std::shared_ptr<Parameter>> parameters;
  absl::flat_hash_map<string, double> gradients;
  interleave_many->CollectTunableParameters(&parameters);
  parameters[async_known_many->long_name()]->value = 1;
  double output_time = interleave_many->OutputTime(&input_times, &gradients);
  parameters[async_known_many->long_name()]->value += kParameterStep;
  double new_output_time = interleave_many->OutputTime(&input_times, nullptr);
  EXPECT_NEAR(gradients[async_known_many->long_name()],
              (new_output_time - output_time) / kParameterStep,
              kComparisonPrecision);
}

TEST(KnownRatioGradientTest, Model) {
  const int64 parallelism = model::kAutotune;
  const double input_time = 100;
  const int64 num_inputs_per_output = 2;
  std::shared_ptr<Node> known_many = model::MakeKnownRatioNode(
      {0, "known_many", nullptr}, num_inputs_per_output);
  std::shared_ptr<Node> async_known_many = model::MakeAsyncKnownRatioNode(
      {1, "async_known_many", known_many}, num_inputs_per_output,
      {model::MakeParameter(
          "parallelism",
          std::make_shared<SharedState>(parallelism, nullptr, nullptr), 1,
          parallelism)});
  known_many->record_element();
  known_many->add_processing_time(100);
  known_many->add_input(async_known_many);
  async_known_many->record_element();
  async_known_many->add_processing_time(300);
  absl::flat_hash_map<string, double> input_times;
  input_times[kModelInputTimeKey] = input_time;
  absl::flat_hash_map<string, std::shared_ptr<Parameter>> parameters;
  absl::flat_hash_map<string, double> gradients;
  known_many->CollectTunableParameters(&parameters);
  parameters[async_known_many->long_name()]->value = 1;
  double output_time = known_many->OutputTime(&input_times, &gradients);
  parameters[async_known_many->long_name()]->value += kParameterStep;
  double new_output_time = known_many->OutputTime(&input_times, nullptr);
  EXPECT_NEAR(gradients[async_known_many->long_name()],
              (new_output_time - output_time) / kParameterStep,
              kComparisonPrecision);
}

TEST(UnknownRatioGradientTest, Model) {
  const int64 parallelism = model::kAutotune;
  const double input_time = 100;
  const int64 num_inputs_per_output = 2;
  std::shared_ptr<Node> unknown_many =
      model::MakeUnknownRatioNode({0, "unknown_many", nullptr});
  std::shared_ptr<Node> async_known_many = model::MakeAsyncKnownRatioNode(
      {1, "async_known_many", unknown_many}, num_inputs_per_output,
      {model::MakeParameter(
          "parallelism",
          std::make_shared<SharedState>(parallelism, nullptr, nullptr), 1,
          parallelism)});
  unknown_many->record_element();
  unknown_many->add_processing_time(100);
  unknown_many->add_input(async_known_many);
  async_known_many->record_element();
  async_known_many->add_processing_time(300);
  absl::flat_hash_map<string, double> input_times;
  input_times[kModelInputTimeKey] = input_time;
  absl::flat_hash_map<string, std::shared_ptr<Parameter>> parameters;
  absl::flat_hash_map<string, double> gradients;
  unknown_many->CollectTunableParameters(&parameters);
  parameters[async_known_many->long_name()]->value = 1;
  double output_time = unknown_many->OutputTime(&input_times, &gradients);
  parameters[async_known_many->long_name()]->value += kParameterStep;
  double new_output_time = unknown_many->OutputTime(&input_times, nullptr);
  EXPECT_NEAR(gradients[async_known_many->long_name()],
              (new_output_time - output_time) / kParameterStep,
              kComparisonPrecision);
}

TEST(UnknownGradientTest, Model) {
  const int64 parallelism = model::kAutotune;
  const double input_time = 100;
  const int64 num_inputs_per_output = 2;
  std::shared_ptr<Node> unknown =
      model::MakeUnknownNode({0, "unknown", nullptr});
  std::shared_ptr<Node> async_known_many = model::MakeAsyncKnownRatioNode(
      {1, "async_known_many", unknown}, num_inputs_per_output,
      {model::MakeParameter(
          "parallelism",
          std::make_shared<SharedState>(parallelism, nullptr, nullptr), 1,
          parallelism)});
  unknown->record_element();
  unknown->add_processing_time(100);
  unknown->add_input(async_known_many);
  async_known_many->record_element();
  async_known_many->add_processing_time(300);
  absl::flat_hash_map<string, double> input_times;
  input_times[kModelInputTimeKey] = input_time;
  absl::flat_hash_map<string, std::shared_ptr<Parameter>> parameters;
  absl::flat_hash_map<string, double> gradients;
  unknown->CollectTunableParameters(&parameters);
  parameters[async_known_many->long_name()]->value = 1;
  double output_time = unknown->OutputTime(&input_times, &gradients);
  parameters[async_known_many->long_name()]->value += kParameterStep;
  double new_output_time = unknown->OutputTime(&input_times, nullptr);
  EXPECT_NEAR(gradients[async_known_many->long_name()],
              (new_output_time - output_time) / kParameterStep,
              kComparisonPrecision);
}

TEST(SnapshotTest, Model) {
  std::shared_ptr<Node> root =
      model::MakeUnknownNode({0, std::to_string(0), nullptr});
  std::shared_ptr<Node> current = root;

  int64 num_nodes = 20;
  for (int64 i = 1; i < num_nodes; i++) {
    std::shared_ptr<Node> input =
        model::MakeUnknownNode({i, std::to_string(i), current});
    input->set_autotune(std::rand() % 2 == 1);
    current->add_input(input);
    current = input;
  }

  std::shared_ptr<Node> cloned_root = root->Snapshot();
  current = root;
  std::shared_ptr<Node> cloned_current = cloned_root;

  for (int64 i = 0; i < num_nodes; i++) {
    EXPECT_EQ(current->id(), cloned_current->id());
    EXPECT_EQ(current->name(), cloned_current->name());
    EXPECT_EQ(current->autotune(), cloned_current->autotune());
    EXPECT_NE(current.get(), cloned_current.get());

    if (i > 0) {
      EXPECT_EQ(current->output()->long_name(),
                cloned_current->output()->long_name());
      EXPECT_EQ(current->output()->autotune(),
                cloned_current->output()->autotune());
      EXPECT_NE(current->output(), cloned_current->output());
    } else {
      EXPECT_EQ(current->output(), nullptr);
      EXPECT_EQ(cloned_current->output(), nullptr);
    }

    if (i < num_nodes - 1) {
      current = current->inputs().front();
      cloned_current = cloned_current->inputs().front();
    }
  }
}

class ComputeWaitTimeTest
    : public ::testing::TestWithParam<std::tuple<double, double, double>> {};

TEST_P(ComputeWaitTimeTest, Model) {
  const double producer_time = std::get<0>(GetParam());
  const double consumer_time = std::get<1>(GetParam());
  const double buffer_size = std::get<2>(GetParam());

  double producer_time_derivative = 0.0L;
  double consumer_time_derivative = 0.0L;
  double buffer_size_derivative = 0.0L;

  double wait_time = model::Node::ComputeWaitTime(
      producer_time, consumer_time, buffer_size, &producer_time_derivative,
      &consumer_time_derivative, &buffer_size_derivative);

  double new_wait_time = model::Node::ComputeWaitTime(
      producer_time + kParameterStep, consumer_time, buffer_size, nullptr,
      nullptr, nullptr);
  EXPECT_NEAR(producer_time_derivative,
              (new_wait_time - wait_time) / kParameterStep,
              kComparisonPrecision);

  if (producer_time >= kParameterStep) {
    new_wait_time = model::Node::ComputeWaitTime(producer_time - kParameterStep,
                                                 consumer_time, buffer_size,
                                                 nullptr, nullptr, nullptr);
    EXPECT_NEAR(producer_time_derivative,
                (wait_time - new_wait_time) / kParameterStep,
                kComparisonPrecision);
  }

  new_wait_time = model::Node::ComputeWaitTime(
      producer_time, consumer_time + kParameterStep, buffer_size, nullptr,
      nullptr, nullptr);
  EXPECT_NEAR(consumer_time_derivative,
              (new_wait_time - wait_time) / kParameterStep,
              kComparisonPrecision);

  if (consumer_time >= kParameterStep) {
    new_wait_time = model::Node::ComputeWaitTime(
        producer_time, consumer_time - kParameterStep, buffer_size, nullptr,
        nullptr, nullptr);
    EXPECT_NEAR(consumer_time_derivative,
                (wait_time - new_wait_time) / kParameterStep,
                kComparisonPrecision);
  }

  new_wait_time = model::Node::ComputeWaitTime(producer_time, consumer_time,
                                               buffer_size + kParameterStep,
                                               nullptr, nullptr, nullptr);
  EXPECT_NEAR(buffer_size_derivative,
              (new_wait_time - wait_time) / kParameterStep,
              kComparisonPrecision);

  if (buffer_size >= kParameterStep) {
    new_wait_time = model::Node::ComputeWaitTime(producer_time, consumer_time,
                                                 buffer_size - kParameterStep,
                                                 nullptr, nullptr, nullptr);
    EXPECT_NEAR(buffer_size_derivative,
                (wait_time - new_wait_time) / kParameterStep,
                kComparisonPrecision);
  }
}

INSTANTIATE_TEST_SUITE_P(
    Test, ComputeWaitTimeTest,
    ::testing::Combine(::testing::Values(0, 20, 40, 80, 100),
                       ::testing::Values(0, 20, 40, 80, 100),
                       ::testing::Values(0, 1, 2, 4, 10, 20, 40)));

class SelfProcessingTimeTest : public ::testing::TestWithParam<int64> {};

TEST_P(SelfProcessingTimeTest, Model) {
  const int64 add_times = GetParam();
  std::shared_ptr<Node> source = model::MakeSourceNode({0, "source", nullptr});
  for (int i = 0; i < add_times; i++) {
    source->add_processing_time(i);
    source->record_element();
  }
  double self_processing_time =
      (add_times == 0 ? 0.0 : (static_cast<double>(add_times) - 1.0) / 2.0);
  EXPECT_EQ(source->SelfProcessingTime(), self_processing_time);
}

INSTANTIATE_TEST_SUITE_P(Test, SelfProcessingTimeTest,
                         ::testing::Values(0, 1, 2, 5, 10, 20, 40));

class OptimizeZeroRamBudgetTest
    : public ::testing::TestWithParam<model::AutotuneAlgorithm> {};

TEST_P(OptimizeZeroRamBudgetTest, Model) {
  const model::AutotuneAlgorithm algorithm = GetParam();

  std::shared_ptr<mutex> mutex1 = std::make_shared<mutex>();
  std::shared_ptr<condition_variable> cv1 =
      std::make_shared<condition_variable>();
  std::shared_ptr<Node> node1 = model::MakeAsyncKnownRatioNode(
      {1, "1", nullptr}, 2,
      {model::MakeParameter("parallelism",
                            std::make_shared<SharedState>(-1, mutex1, cv1), 1,
                            5)});
  node1->record_buffer_event(1, 1);

  std::shared_ptr<mutex> mutex2 = std::make_shared<mutex>();
  std::shared_ptr<condition_variable> cv2 =
      std::make_shared<condition_variable>();
  std::shared_ptr<Node> node2 = model::MakeAsyncKnownRatioNode(
      {2, "2", node1}, 5,
      {model::MakeParameter("buffer_size",
                            std::make_shared<SharedState>(-1, mutex2, cv2), 0,
                            6)});
  node2->record_buffer_event(1, 1);

  std::shared_ptr<mutex> mutex3 = std::make_shared<mutex>();
  std::shared_ptr<condition_variable> cv3 =
      std::make_shared<condition_variable>();
  std::shared_ptr<Node> node3 = model::MakeAsyncInterleaveManyNode(
      {3, "3", node2},
      {model::MakeParameter("parallelism",
                            std::make_shared<SharedState>(-1, mutex3, cv3), 1,
                            7)});
  node3->record_buffer_event(1, 1);

  EXPECT_EQ(node1->parameter_value("parallelism"), -1);
  EXPECT_EQ(node2->parameter_value("buffer_size"), -1);
  EXPECT_EQ(node3->parameter_value("parallelism"), -1);

  model::Model model;
  model.AddNode([&node1](model::Node::Args args) { return node1; }, "1",
                nullptr, &node1);
  model.AddNode([&node2](model::Node::Args args) { return node2; }, "2", node1,
                &node2);
  model.AddNode([&node3](model::Node::Args args) { return node3; }, "3", node2,
                &node3);

  model.Optimize(algorithm, 40, 0, 0);
  EXPECT_EQ(node1->parameter_value("parallelism"), 1);
  EXPECT_EQ(node2->parameter_value("buffer_size"), 0);
  EXPECT_EQ(node3->parameter_value("parallelism"), 1);
}

INSTANTIATE_TEST_SUITE_P(Test, OptimizeZeroRamBudgetTest,
                         ::testing::Values(0, 1));

}  // namespace
}  // namespace model
}  // namespace data
}  // namespace tensorflow
