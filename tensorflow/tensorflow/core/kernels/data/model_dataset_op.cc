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
#include "tensorflow/core/kernels/data/model_dataset_op.h"

// On mobile we do not provide model dataset op because not all of its
// dependencies are available there. The op is replaced with a no-op.
#if !defined(IS_MOBILE_PLATFORM)
#include "absl/memory/memory.h"
#include "tensorflow/core/framework/dataset.h"
#include "tensorflow/core/framework/metrics.h"
#include "tensorflow/core/framework/model.h"
#include "tensorflow/core/framework/partial_tensor_shape.h"
#include "tensorflow/core/framework/tensor.h"
#include "tensorflow/core/lib/random/random.h"
#include "tensorflow/core/platform/cpu_info.h"
#include "tensorflow/core/util/ptr_util.h"

namespace tensorflow {
namespace data {
namespace {

constexpr int64 kOptimizationPeriodThresholdMs = 60 * EnvTime::kSecondsToMillis;

// Default share of available RAM that can be used by model's internal buffers.
constexpr double kRamBudgetShare = 0.5;

}  // namespace

/* static */ constexpr const char* const ModelDatasetOp::kAlgorithm;
/* static */ constexpr const char* const ModelDatasetOp::kCpuBudget;
/* static */ constexpr const char* const ModelDatasetOp::kRamBudget;

class ModelDatasetOp::Dataset : public DatasetBase {
 public:
  Dataset(OpKernelContext* ctx, const DatasetBase* input,
          model::AutotuneAlgorithm algorithm, int64 cpu_budget,
          int64 ram_budget)
      : DatasetBase(DatasetContext(ctx)),
        input_(input),
        algorithm_(algorithm),
        cpu_budget_(cpu_budget),
        ram_budget_(ram_budget) {
    input_->Ref();
  }

  ~Dataset() override { input_->Unref(); }

  std::unique_ptr<IteratorBase> MakeIteratorInternal(
      const string& prefix) const override {
    return absl::make_unique<Iterator>(
        Iterator::Params{this, strings::StrCat(prefix, "::Model")});
  }

  const DataTypeVector& output_dtypes() const override {
    return input_->output_dtypes();
  }
  const std::vector<PartialTensorShape>& output_shapes() const override {
    return input_->output_shapes();
  }

  string DebugString() const override { return "ModelDatasetOp::Dataset"; }

  int64 Cardinality() const override { return input_->Cardinality(); }

  Status InputDatasets(std::vector<const DatasetBase*>* inputs) const override {
    inputs->push_back(input_);
    return Status::OK();
  }

  Status CheckExternalState() const override {
    return input_->CheckExternalState();
  }

 protected:
  Status AsGraphDefInternal(SerializationContext* ctx,
                            DatasetGraphDefBuilder* b,
                            Node** output) const override {
    Node* input_graph_node = nullptr;
    TF_RETURN_IF_ERROR(b->AddInputDataset(ctx, input_, &input_graph_node));
    TF_RETURN_IF_ERROR(b->AddDataset(this, {input_graph_node}, output));
    AttrValue algorithm_attr;
    b->BuildAttrValue(static_cast<int64>(algorithm_), &algorithm_attr);
    AttrValue cpu_budget_attr;
    b->BuildAttrValue(cpu_budget_, &cpu_budget_attr);
    AttrValue ram_budget_attr;
    b->BuildAttrValue(ram_budget_, &ram_budget_attr);

    TF_RETURN_IF_ERROR(
        b->AddDataset(this, {input_graph_node},
                      {std::make_pair(kAlgorithm, algorithm_attr),
                       std::make_pair(kCpuBudget, cpu_budget_attr),
                       std::make_pair(kRamBudget, ram_budget_attr)},
                      output));
    return Status::OK();
  }

 private:
  class Iterator : public DatasetIterator<Dataset> {
   public:
    explicit Iterator(const Params& params)
        : DatasetIterator<Dataset>(params),
          cpu_budget_(dataset()->cpu_budget_ == 0 ? port::NumSchedulableCPUs()
                                                  : dataset()->cpu_budget_),
          ram_budget_(dataset()->ram_budget_ == 0
                          ? kRamBudgetShare * port::AvailableRam()
                          : dataset()->ram_budget_) {
      model_ = std::make_shared<model::Model>();
    }

    ~Iterator() override {
      // Signal the optimize thread to terminate it. We will then join that
      // thread when we delete `this->optimize_thread_`.
      mutex_lock l(mu_);
      cancelled_ = true;
      cond_var_.notify_all();
    }

    Status Initialize(IteratorContext* ctx) override {
      IteratorContext::Params params(ctx);
      params.model = model_;
      return dataset()->input_->MakeIterator(IteratorContext(std::move(params)),
                                             this, prefix(), &input_impl_);
    }

    Status GetNextInternal(IteratorContext* ctx,
                           std::vector<Tensor>* out_tensors,
                           bool* end_of_sequence) override {
      IteratorContext::Params params(ctx);
      {
        mutex_lock l(mu_);
        TF_RETURN_IF_ERROR(EnsureOptimizeThreadStarted(ctx));
        params.model = model_;
        int64 now_nanos = EnvTime::NowNanos();
        RecordInput(now_nanos);
      }
      Status s = input_impl_->GetNext(IteratorContext(std::move(params)),
                                      out_tensors, end_of_sequence);
      int64 now_nanos = EnvTime::NowNanos();
      mutex_lock l(mu_);
      RecordOutput(now_nanos);
      return s;
    }

   protected:
    std::shared_ptr<model::Node> CreateNode(
        IteratorContext* ctx, model::Node::Args args) const override {
      return model::MakeKnownRatioNode(std::move(args),
                                       /*ratio=*/1);
    }

    Status SaveInternal(SerializationContext* ctx,
                        IteratorStateWriter* writer) override {
      mutex_lock l(mu_);
      TF_RETURN_IF_ERROR(SaveInput(ctx, writer, input_impl_));
      return Status::OK();
    }

    Status RestoreInternal(IteratorContext* ctx,
                           IteratorStateReader* reader) override {
      mutex_lock l(mu_);
      TF_RETURN_IF_ERROR(RestoreInput(ctx, reader, input_impl_));
      return Status::OK();
    }

   private:
    Status EnsureOptimizeThreadStarted(IteratorContext* ctx)
        TF_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
      if (!model_thread_) {
        std::shared_ptr<IteratorContext> new_ctx =
            std::make_shared<IteratorContext>(*ctx);
        model_thread_ = ctx->StartThread(
            "tf_data_model", [this, new_ctx]() { ModelThread(new_ctx); });
      }
      return Status::OK();
    }

    void ModelThread(const std::shared_ptr<IteratorContext>& ctx) {
      int64 last_optimization_ms = 0;
      int64 optimization_period_ms = 10;
      int64 current_time_ms = EnvTime::NowMicros() / EnvTime::kMillisToMicros;
      while (true) {
        {
          mutex_lock l(mu_);
          while (!cancelled_ && last_optimization_ms + optimization_period_ms >
                                    current_time_ms) {
            auto wait_ms =
                last_optimization_ms + optimization_period_ms - current_time_ms;
            VLOG(2) << "Waiting for " << wait_ms << " ms.";
            cond_var_.wait_for(l, std::chrono::milliseconds(wait_ms));
            current_time_ms = EnvTime::NowMicros() / EnvTime::kMillisToMicros;
          }
          if (cancelled_) return;
        }
        double model_input_time;
        {
          tf_shared_lock l(mu_);
          model_input_time = SelfInputTime();
        }
        model_->Optimize(dataset()->algorithm_, cpu_budget_, ram_budget_,
                         /*model_input_time=*/0);
        // Exponentially increase the period of running the optimization
        // until a threshold is reached.
        if (optimization_period_ms != kOptimizationPeriodThresholdMs) {
          optimization_period_ms = std::min(optimization_period_ms << 1,
                                            kOptimizationPeriodThresholdMs);
        }
        current_time_ms = EnvTime::NowMicros() / EnvTime::kMillisToMicros;
        last_optimization_ms = current_time_ms;
        model_->FlushMetrics();
      }
    }

    void RecordInput(int64 time_nanos) TF_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
      if (last_output_time_ != 0) {
        DCHECK_LE(last_output_time_, time_nanos);
        input_time_ += time_nanos - last_output_time_;
        num_input_events_++;
      }
    }

    void RecordOutput(int64 time_nanos) TF_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
      last_output_time_ = time_nanos;
    }

    double SelfInputTime() const TF_SHARED_LOCKS_REQUIRED(mu_) {
      if (num_input_events_ == 0) {
        return 0;
      }
      return static_cast<double>(input_time_) /
             static_cast<double>(num_input_events_);
    }

    mutex mu_;
    condition_variable cond_var_;
    std::shared_ptr<model::Model> model_;
    std::unique_ptr<Thread> model_thread_ TF_GUARDED_BY(mu_);
    bool cancelled_ TF_GUARDED_BY(mu_) = false;
    std::unique_ptr<IteratorBase> input_impl_;
    int64 num_input_events_ TF_GUARDED_BY(mu_) = 0;
    int64 input_time_ TF_GUARDED_BY(mu_) = 0;
    int64 last_output_time_ TF_GUARDED_BY(mu_) = 0;
    const int64 cpu_budget_;
    const int64 ram_budget_;
  };

  const DatasetBase* input_;
  const model::AutotuneAlgorithm algorithm_;
  const int64 cpu_budget_;
  const int64 ram_budget_;
};

ModelDatasetOp::ModelDatasetOp(OpKernelConstruction* ctx)
    : UnaryDatasetOpKernel(ctx) {
  if (ctx->HasAttr(kAlgorithm)) {
    int64 algorithm;
    OP_REQUIRES_OK(ctx, ctx->GetAttr(kAlgorithm, &algorithm));
    algorithm_ = model::AutotuneAlgorithm(algorithm);
  } else {
    algorithm_ = model::AutotuneAlgorithm::HILL_CLIMB;
  }
  OP_REQUIRES_OK(ctx, ctx->GetAttr(kCpuBudget, &cpu_budget_));
  OP_REQUIRES(ctx, cpu_budget_ >= 0,
              errors::InvalidArgument("CPU budget must be positive but is ",
                                      cpu_budget_, "."));
  if (ctx->HasAttr(kRamBudget)) {
    OP_REQUIRES_OK(ctx, ctx->GetAttr(kRamBudget, &ram_budget_));
  } else {
    ram_budget_ = 0;
  }
  OP_REQUIRES(ctx, ram_budget_ >= 0,
              errors::InvalidArgument("RAM budget must be positive but is ",
                                      ram_budget_, "."));
}

void ModelDatasetOp::MakeDataset(OpKernelContext* ctx, DatasetBase* input,
                                 DatasetBase** output) {
  *output = new ModelDatasetOp::Dataset(ctx, input, algorithm_, cpu_budget_,
                                        ram_budget_);
}

namespace {
REGISTER_KERNEL_BUILDER(Name("ModelDataset").Device(DEVICE_CPU),
                        ModelDatasetOp);
}  // namespace
}  // namespace data
}  // namespace tensorflow
#else  // !IS_MOBILE_PLATFORM
namespace tensorflow {
namespace data {

ModelDatasetOp::ModelDatasetOp(OpKernelConstruction* ctx)
    : UnaryDatasetOpKernel(ctx) {}

void ModelDatasetOp::MakeDataset(OpKernelContext* ctx, DatasetBase* input,
                                 DatasetBase** output) {
  input->Ref();
  *output = input;
}

namespace {
REGISTER_KERNEL_BUILDER(Name("ModelDataset").Device(DEVICE_CPU),
                        ModelDatasetOp);
}  // namespace
}  // namespace data
}  // namespace tensorflow
#endif  // !IS_MOBILE_PLATFORM
