/* Copyright 2017 The TensorFlow Authors. All Rights Reserved.

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

#include "absl/strings/str_cat.h"
#include "tensorflow/core/framework/function.h"
#include "tensorflow/core/framework/op_kernel.h"
#include "tensorflow/core/framework/resource_mgr.h"
#include "tensorflow/core/framework/tensor.h"
#include "tensorflow/core/framework/tensor_util.h"
#include "tensorflow/core/framework/types.h"
#include "tensorflow/core/kernels/batching_util/batch_resource_base.h"
#include "tensorflow/core/kernels/batching_util/concat_split_util.h"
#include "tensorflow/core/kernels/batching_util/periodic_function.h"
#include "tensorflow/core/kernels/ops_util.h"
#include "tensorflow/core/lib/monitoring/gauge.h"
#include "tensorflow/core/lib/random/random.h"
#include "tensorflow/core/platform/errors.h"
#include "tensorflow/core/platform/logging.h"
#include "tensorflow/core/platform/macros.h"

namespace tensorflow {

auto* batch_op_split_usage = monitoring::Gauge<string, 1>::New(
    "/tensorflow/serving/batching/enable_large_batch_splitting",
    "Tracks the usage of attribute `enable_large_batch_splitting` for "
    "BatchFunction kernel in a saved model.",
    "model_name");

void RecordBatchSplitUsage(
    absl::optional<bool> maybe_enable_large_batch_splitting,
    const string& model_name) {
  if (maybe_enable_large_batch_splitting.has_value()) {
    if (maybe_enable_large_batch_splitting.value()) {
      batch_op_split_usage->GetCell(model_name)->Set("true");
    } else {
      batch_op_split_usage->GetCell(model_name)->Set("false");
    }
  } else {
    batch_op_split_usage->GetCell(model_name)->Set("unset");
  }
}

const string& GetModelName(OpKernelContext* ctx) {
  static string* kModelNameUnset = new string("model_name_unset");
  if (!ctx->session_metadata()) return *kModelNameUnset;
  if (ctx->session_metadata()->name().empty()) return *kModelNameUnset;
  return ctx->session_metadata()->name();
}

using ::tensorflow::concat_split_util::Concat;
using ::tensorflow::concat_split_util::Split;

// A class encapsulating the state and logic for batching tensors.
class BatchResource : public serving::BatchResourceBase {
 public:
  static Status Create(int32 num_batch_threads, int32 max_batch_size,
                       int32 batch_timeout_micros, int32 max_enqueued_batches,
                       const std::vector<int32>& allowed_batch_sizes,
                       FunctionLibraryRuntime::Handle fhandle,
                       bool enable_large_batch_splitting,
                       std::unique_ptr<BatchResource>* resource) {
    BatcherT::Options batcher_options;
    batcher_options.num_batch_threads = num_batch_threads;
    std::shared_ptr<BatcherT> batcher;
    TF_RETURN_IF_ERROR(BatcherT::Create(batcher_options, &batcher));

    resource->reset(new BatchResource(
        fhandle, std::move(batcher),
        GetBatcherQueueOptions(num_batch_threads, max_batch_size,
                               batch_timeout_micros, max_enqueued_batches,
                               allowed_batch_sizes,
                               enable_large_batch_splitting),
        allowed_batch_sizes));
    return Status::OK();
  }

  string DebugString() const final { return "BatchResource"; }

 private:
  BatchResource(FunctionLibraryRuntime::Handle fhandle,
                std::shared_ptr<BatcherT> batcher,
                const BatcherT::QueueOptions& batcher_queue_options,
                std::vector<int32> allowed_batch_sizes)
      : BatchResourceBase(
            /*has_process_batch_function=*/fhandle != kInvalidHandle,
            std::move(batcher), batcher_queue_options,
            std::move(allowed_batch_sizes)),
        fhandle_(fhandle) {}

  void ProcessFuncBatchImpl(
      const BatchTask& last_task, absl::Span<const Tensor> inputs,
      std::vector<Tensor>* combined_outputs,
      std::function<void(const Status&)> done) const override {
    auto* last_task_context = last_task.context;
    FunctionLibraryRuntime::Options opts;
    opts.step_container = last_task_context->step_container();
    opts.cancellation_manager = last_task_context->cancellation_manager();
    opts.collective_executor = last_task_context->collective_executor();
    opts.stats_collector = last_task_context->stats_collector();
    opts.rendezvous = last_task_context->rendezvous();
    opts.runner = last_task_context->runner();
    opts.run_all_kernels_inline = last_task_context->run_all_kernels_inline();
    auto* flib = last_task_context->function_library();
    Notification done_notif;
    flib->Run(opts, fhandle_, inputs, combined_outputs,
              [&](const Status& run_status) {
                done(run_status);
                done_notif.Notify();
              });
    // By waiting for the notification we are ensuring that this thread isn't
    // used for processing other batches, which gives the batches time to
    // coalesce upstream. So overall the number of batches going through the
    // devices goes down, improving latency and throughput in most cases.
    done_notif.WaitForNotification();
  }

  FunctionLibraryRuntime::Handle fhandle_;
};

class BatchFunctionKernel : public AsyncOpKernel {
 public:
  explicit BatchFunctionKernel(OpKernelConstruction* c) : AsyncOpKernel(c) {
    OP_REQUIRES_OK(c, c->GetAttr("container", &container_));
    OP_REQUIRES_OK(c, c->GetAttr("shared_name", &shared_name_));
    // If shared_name is not supplied, use name instead (prevent collisions by
    // default).
    if (shared_name_.empty()) {
      shared_name_ = name();
    }
    OP_REQUIRES_OK(c, c->GetAttr("batching_queue", &batcher_queue_));
    OP_REQUIRES_OK(c, c->GetAttr("num_batch_threads", &num_batch_threads_));
    OP_REQUIRES_OK(c, c->GetAttr("max_batch_size", &max_batch_size_));
    OP_REQUIRES_OK(c,
                   c->GetAttr("batch_timeout_micros", &batch_timeout_micros_));
    OP_REQUIRES_OK(c,
                   c->GetAttr("max_enqueued_batches", &max_enqueued_batches_));
    OP_REQUIRES_OK(c, c->GetAttr("allowed_batch_sizes", &allowed_batch_sizes_));

    auto lib = c->function_library();
    OP_REQUIRES(c, lib != nullptr, errors::Internal("No function library"));
    NameAttrList func;
    OP_REQUIRES_OK(c, c->GetAttr("f", &func));
    OP_REQUIRES_OK(
        c, lib->Instantiate(func.name(), AttrSlice(&func.attr()), &fhandle_));

    if (c->HasAttr("enable_large_batch_splitting")) {
      OP_REQUIRES_OK(c, c->GetAttr("enable_large_batch_splitting",
                                   &enable_large_batch_splitting_));
      has_attribute_enable_large_batch_splitting_ = true;
    } else {
      enable_large_batch_splitting_ = false;
      has_attribute_enable_large_batch_splitting_ = false;
    }

    OP_REQUIRES_OK(c, ValidateAllowedBatchSizes());
  }

  bool IsExpensive() override { return false; }

  void ComputeAsync(OpKernelContext* c, DoneCallback done) final {
    RecordBatchSplitUsage(
        has_attribute_enable_large_batch_splitting_
            ? absl::make_optional(enable_large_batch_splitting_)
            : absl::nullopt,
        GetModelName(c));
    BatchResource* br;
    std::function<Status(BatchResource**)> creator = [this](BatchResource** r) {
      std::unique_ptr<BatchResource> new_resource;
      TF_RETURN_IF_ERROR(BatchResource::Create(
          num_batch_threads_, max_batch_size_, batch_timeout_micros_,
          max_enqueued_batches_, allowed_batch_sizes_, fhandle_,
          enable_large_batch_splitting_, &new_resource));
      *r = new_resource.release();
      return Status::OK();
    };
    OP_REQUIRES_OK_ASYNC(c,
                         c->resource_manager()->LookupOrCreate(
                             container_, shared_name_, &br, creator),
                         done);
    const Status status =
        br->RegisterInput(random::New64(), c, batcher_queue_, done);
    br->Unref();
    OP_REQUIRES_OK_ASYNC(c, status, done);
    // Assume br calls done, so nothing to do here.
  }

  // Validates 'allowed_batch_sizes_'. The entries must increase monotonically,
  // and the last one must equal 'max_batch_size_'.
  Status ValidateAllowedBatchSizes() const {
    if (allowed_batch_sizes_.empty()) {
      return Status::OK();
    }
    int32 last_size = 0;
    for (size_t i = 0; i < allowed_batch_sizes_.size(); ++i) {
      const int32 size = allowed_batch_sizes_.at(i);
      if (i > 0 && size <= last_size) {
        return errors::InvalidArgument(
            "allowed_batch_sizes entries must be monotonically increasing");
      }

      if ((!enable_large_batch_splitting_) &&
          (i == allowed_batch_sizes_.size() - 1) && (size != max_batch_size_)) {
        return errors::InvalidArgument(
            "final entry in allowed_batch_sizes must equal max_batch_size when "
            "enable_large_batch_splitting is False");
      }

      last_size = size;
    }
    return Status::OK();
  }

 private:
  string container_;
  string shared_name_;
  string batcher_queue_;
  int32 num_batch_threads_;
  int32 max_batch_size_;
  int32 batch_timeout_micros_;
  int32 max_enqueued_batches_;
  std::vector<int32> allowed_batch_sizes_;
  FunctionLibraryRuntime::Handle fhandle_;
  bool enable_large_batch_splitting_;
  bool has_attribute_enable_large_batch_splitting_;
};

REGISTER_KERNEL_BUILDER(Name("BatchFunction").Device(DEVICE_CPU),
                        BatchFunctionKernel);

class BatchKernel : public AsyncOpKernel {
 public:
  explicit BatchKernel(OpKernelConstruction* c) : AsyncOpKernel(c) {
    OP_REQUIRES_OK(c, c->GetAttr("container", &container_));
    OP_REQUIRES_OK(c, c->GetAttr("shared_name", &shared_name_));
    // If shared_name is not supplied, use name instead (prevent collisions by
    // default).
    if (shared_name_.empty()) {
      shared_name_ = name();
    }
    OP_REQUIRES_OK(c, c->GetAttr("batching_queue", &batcher_queue_));
    OP_REQUIRES_OK(c, c->GetAttr("num_batch_threads", &num_batch_threads_));
    OP_REQUIRES_OK(c, c->GetAttr("max_batch_size", &max_batch_size_));
    OP_REQUIRES_OK(c,
                   c->GetAttr("batch_timeout_micros", &batch_timeout_micros_));
    OP_REQUIRES_OK(c,
                   c->GetAttr("max_enqueued_batches", &max_enqueued_batches_));
    OP_REQUIRES_OK(c, c->GetAttr("allowed_batch_sizes", &allowed_batch_sizes_));
    OP_REQUIRES_OK(c, ValidateAllowedBatchSizes());
  }

  void ComputeAsync(OpKernelContext* c, DoneCallback done) final {
    BatchResource* br;
    std::function<Status(BatchResource**)> creator = [this](BatchResource** r) {
      std::unique_ptr<BatchResource> new_resource;
      TF_RETURN_IF_ERROR(BatchResource::Create(
          num_batch_threads_, max_batch_size_, batch_timeout_micros_,
          max_enqueued_batches_, allowed_batch_sizes_, kInvalidHandle, false,
          &new_resource));
      *r = new_resource.release();
      return Status::OK();
    };
    OP_REQUIRES_OK_ASYNC(c,
                         c->resource_manager()->LookupOrCreate(
                             container_, shared_name_, &br, creator),
                         done);
    const Status status =
        br->RegisterInput(random::New64(), c, batcher_queue_, done);
    br->Unref();
    OP_REQUIRES_OK_ASYNC(c, status, done);
    // Assume br calls done, so nothing to do here.
  }

  // Validates 'allowed_batch_sizes_'. The entries must increase monotonically,
  // and the last one must equal 'max_batch_size_'.
  Status ValidateAllowedBatchSizes() const {
    if (allowed_batch_sizes_.empty()) {
      return Status::OK();
    }
    int32 last_size = 0;
    for (size_t i = 0; i < allowed_batch_sizes_.size(); ++i) {
      const int32 size = allowed_batch_sizes_.at(i);
      if (i > 0 && size <= last_size) {
        return errors::InvalidArgument(
            "allowed_batch_sizes entries must be monotonically increasing");
      }
      if (i == allowed_batch_sizes_.size() - 1 && size != max_batch_size_) {
        return errors::InvalidArgument(
            "final entry in allowed_batch_sizes must equal max_batch_size");
      }
      last_size = size;
    }
    return Status::OK();
  }

 private:
  string container_;
  string shared_name_;
  string batcher_queue_;
  int32 num_batch_threads_;
  int32 max_batch_size_;
  int32 batch_timeout_micros_;
  int32 max_enqueued_batches_;
  std::vector<int32> allowed_batch_sizes_;
};

REGISTER_KERNEL_BUILDER(Name("Batch").Device(DEVICE_CPU), BatchKernel);

// A class encapsulating the state and logic for unbatching tensors.
//
// UnbatchResource keeps two data structures indexed by batch-key: one which has
// the continuations for all concurrent kernels which are waiting for tensors
// and another which has tensors which are waiting for their corresponding
// kernels to run. Whenever a kernel runs, we either grab its tensor if it's
// waiting already, or we insert it in the queue and then look at its tensor to
// see if it can be used to dispatch any stored continuations.
class UnbatchResource : public ResourceBase {
 public:
  explicit UnbatchResource(int32 timeout_micros)
      : timeout_micros_(timeout_micros),
        timeout_enforcer_(new serving::PeriodicFunction(
            [this] { EnforceTimeout(); }, 1000 /* 1 ms */)) {}

  ~UnbatchResource() override {
    // Tear down 'timeout_enforcer_' first, since it accesses other state in
    // this class.
    timeout_enforcer_ = nullptr;
  }

  string DebugString() const final { return "UnbatchResource"; }

  Status Compute(OpKernelContext* context, AsyncOpKernel::DoneCallback done) {
    const Tensor& data_t = context->input(0);
    const Tensor& batch_index_t = context->input(1);

    if (batch_index_t.shape().dim_size(0) > data_t.shape().dim_size(0)) {
      return errors::InvalidArgument(
          "Wrong shape for index tensor. Expected 0th dimension size to be no "
          "greater than ",
          data_t.shape().dim_size(0),
          "; Got: ", batch_index_t.shape().dim_size(0), ".");
    }
    if (batch_index_t.shape().dim_size(1) != 3) {
      return errors::InvalidArgument(
          "Wrong shape for index tensor. Expected 1st dimension size to be 3 ; "
          "Got: ",
          batch_index_t.shape().dim_size(1), ".");
    }

    const int64 batch_key = context->input(2).scalar<int64>()();
    const bool nonempty_input = batch_index_t.dim_size(0) > 0;

    // If we have a non-empty tensor, slice it up.
    // (It is important to do this outside of the critical section below.)
    // The following variables are populated iff 'nonempty_input==true'.
    std::vector<int64> sizes;
    std::vector<int64> batch_keys;
    std::vector<Tensor> split_inputs;
    if (nonempty_input) {
      auto batch_indices =
          batch_index_t.shaped<int64, 2>({batch_index_t.dim_size(0), 3});
      for (int i = 0; i < batch_index_t.dim_size(0); ++i) {
        sizes.push_back(batch_indices(i, 2) - batch_indices(i, 1));
        batch_keys.push_back(batch_indices(i, 0));
      }

      TF_RETURN_IF_ERROR(Split(context, data_t, sizes, &split_inputs));
    }

    // Critical section.
    std::vector<AsyncOpKernel::DoneCallback> done_callbacks_to_call;
    Status status = [&]() -> Status {
      mutex_lock ml(mu_);

      // Check to see whether the tensor we want is already ready.
      auto tensor_it = waiting_tensors_.find(batch_key);
      if (tensor_it != waiting_tensors_.end()) {
        context->set_output(0, tensor_it->second.tensor);
        waiting_tensors_.erase(tensor_it);
        done_callbacks_to_call.push_back(done);
        return Status::OK();
      }

      const uint64 deadline_micros =
          Env::Default()->NowMicros() + timeout_micros_;

      // Add ourselves to the waitlist for tensors.
      if (!waiting_callbacks_
               .emplace(batch_key,
                        WaitingCallback{deadline_micros, context, done})
               .second) {
        return errors::AlreadyExists(
            "Multiple session runs with the same batch key.");
      }

      // If we have a non-empty tensor, finish the waitlisted runs,
      // and store any remaining pieces.
      if (nonempty_input) {
        for (size_t i = 0; i < batch_keys.size(); ++i) {
          auto runs_it = waiting_callbacks_.find(batch_keys[i]);
          if (runs_it != waiting_callbacks_.end()) {
            runs_it->second.context->set_output(0, split_inputs[i]);
            done_callbacks_to_call.push_back(runs_it->second.done);
            waiting_callbacks_.erase(runs_it);
          } else {
            // Note: the deadline here is in case we are arriving late and the
            // kernel that should rendezvous with this tensor has already waited
            // and timed out.
            if (!waiting_tensors_
                     .emplace(batch_keys[i],
                              WaitingTensor{deadline_micros, split_inputs[i]})
                     .second) {
              return errors::AlreadyExists(
                  "Multiple tensors returned for same batch key.");
            }
          }
        }
      }

      return Status::OK();
    }();

    for (const AsyncOpKernel::DoneCallback& done_callback :
         done_callbacks_to_call) {
      done_callback();
    }

    return status;
  }

 private:
  // Evicts waiting tensors and callbacks that have exceeded their deadline.
  void EnforceTimeout() {
    const uint64 now = Env::Default()->NowMicros();
    std::vector<WaitingCallback> evicted_callbacks;

    {
      mutex_lock ml(mu_);

      for (auto it = waiting_tensors_.begin(); it != waiting_tensors_.end();) {
        const WaitingTensor& waiting_tensor = it->second;
        if (waiting_tensor.deadline_micros < now) {
          it = waiting_tensors_.erase(it);
        } else {
          ++it;
        }
      }

      for (auto it = waiting_callbacks_.begin();
           it != waiting_callbacks_.end();) {
        const WaitingCallback& waiting_callback = it->second;
        if (waiting_callback.deadline_micros < now) {
          evicted_callbacks.push_back(waiting_callback);
          it = waiting_callbacks_.erase(it);
        } else {
          ++it;
        }
      }
    }

    for (const WaitingCallback& evicted_callback : evicted_callbacks) {
      evicted_callback.context->CtxFailureWithWarning(errors::DeadlineExceeded(
          "Batched data did not arrive within timeout window."));
      evicted_callback.done();
    }
  }

  struct WaitingTensor {
    uint64 deadline_micros;
    Tensor tensor;
  };

  struct WaitingCallback {
    uint64 deadline_micros;
    OpKernelContext* context;
    AsyncOpKernel::DoneCallback done;
  };

  const int32 timeout_micros_;

  mutex mu_;

  // Maps keyed by BatchKey of tensors waiting for callbacks and callbacks
  // waiting for tensors.
  std::unordered_map<int64, WaitingTensor> waiting_tensors_ TF_GUARDED_BY(mu_);
  std::unordered_map<int64, WaitingCallback> waiting_callbacks_
      TF_GUARDED_BY(mu_);

  // A thread that evicts waiting tensors and callbacks that have exceeded their
  // deadline.
  std::unique_ptr<serving::PeriodicFunction> timeout_enforcer_;
};

class UnbatchKernel : public AsyncOpKernel {
 public:
  explicit UnbatchKernel(OpKernelConstruction* c) : AsyncOpKernel(c) {
    OP_REQUIRES_OK(c, c->GetAttr("container", &container_));
    OP_REQUIRES_OK(c, c->GetAttr("shared_name", &shared_name_));
    // If shared_name is not supplied, use name instead (prevent collisions by
    // default).
    if (shared_name_.empty()) {
      shared_name_ = name();
    }
    OP_REQUIRES_OK(c, c->GetAttr("timeout_micros", &timeout_micros_));
  }

  void ComputeAsync(OpKernelContext* c, DoneCallback done) final {
    UnbatchResource* ubr;
    std::function<Status(UnbatchResource**)> creator =
        [this](UnbatchResource** r) {
          *r = new UnbatchResource(timeout_micros_);
          return Status::OK();
        };
    OP_REQUIRES_OK_ASYNC(c,
                         c->resource_manager()->LookupOrCreate(
                             container_, shared_name_, &ubr, creator),
                         done);
    auto status = ubr->Compute(c, done);
    ubr->Unref();
    OP_REQUIRES_OK_ASYNC(c, status, done);
    // Assume ubr calls done, so nothing to do here.
  }

 private:
  string container_;
  string shared_name_;
  int32 timeout_micros_;
};
REGISTER_KERNEL_BUILDER(Name("Unbatch").Device(DEVICE_CPU), UnbatchKernel);

// A class encapsulating the state and logic for batching tensors
// deterministically for the gradient of unbatch.
class UnbatchGradResource : public ResourceBase {
 public:
  UnbatchGradResource() {}

  string DebugString() const final { return "UnbatchGradResource"; }

  // Flushes the information for one batch, given its context and done
  // callback. Clears all information about it from the available_tensors_.
  Status OutputBatch(OpKernelContext* context,
                     const AsyncOpKernel::DoneCallback& done)
      TF_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    const Tensor& batch_index_t = context->input(1);
    auto batch_index =
        batch_index_t.shaped<int64, 2>({batch_index_t.dim_size(0), 3});
    std::vector<Tensor> tensors;
    for (int i = 0; i < batch_index_t.dim_size(0); ++i) {
      auto available_it = available_tensors_.find(batch_index(i, 0));
      if (available_it == available_tensors_.end()) {
        return errors::Internal("bad bookkeeping of available tensors.");
      }
      tensors.push_back(available_it->second);
      available_tensors_.erase(available_it);
    }

    const DataType type = tensors[0].dtype();
    Tensor concatenated_tensor;
    switch (type) {
#define CASE(type)                                                            \
  case DataTypeToEnum<type>::value:                                           \
    TF_RETURN_IF_ERROR(Concat<type>(context, tensors, &concatenated_tensor)); \
    context->set_output(0, concatenated_tensor);                              \
    break;
      TF_CALL_ALL_TYPES(CASE);
#undef CASE
      default:
        return errors::InvalidArgument("Unsupported data type: ", type);
    }
    done();
    return Status::OK();
  }

  // Ingests data from one invocation of the op.
  Status Compute(OpKernelContext* context,
                 const AsyncOpKernel::DoneCallback& done) {
    const Tensor& data_t = context->input(0);
    const Tensor& batch_index_t = context->input(1);
    const Tensor& grad_t = context->input(2);

    mutex_lock ml(mu_);

    const int64 batch_key = context->input(3).scalar<int64>()();
    // Mark our tensor as available.
    if (!available_tensors_.emplace(batch_key, grad_t).second) {
      return errors::InvalidArgument("Two runs with the same batch key.");
    }

    // Check whether we have a valid input tensor and, if so, create its
    // dispatch logic.
    if (data_t.NumElements() > 0) {
      if (batch_index_t.NumElements() == 0) {
        return errors::InvalidArgument(
            "batch_index is empty while the tensor isn't.");
      }
      std::unordered_set<int64> missing_tensors;
      const auto batch_index =
          batch_index_t.shaped<int64, 2>({batch_index_t.dim_size(0), 3});
      for (int i = 0; i < batch_index_t.dim_size(0); ++i) {
        const int64 batch_key = batch_index(i, 0);
        if (available_tensors_.find(batch_key) == available_tensors_.end()) {
          missing_tensors.emplace(batch_key);
        }
      }
      if (missing_tensors.empty()) {
        return OutputBatch(context, done);
      }
      if (!available_batches_
               .emplace(batch_key, Batch{missing_tensors, context, done})
               .second) {
        return errors::InvalidArgument(
            "Batch key with valid batch used twice.");
      }
      for (const int64 i : missing_tensors) {
        if (!desired_tensor_to_batch_map_.emplace(i, batch_key).second) {
          return errors::InvalidArgument(
              "Missing tensor wanted by more than one batch.");
        }
      }
    } else {
      // If we don't have a valid input tensor we can output an empty tensor and
      // call our done closure.
      TensorShape output_shape(grad_t.shape());
      output_shape.set_dim(0, 0);
      Tensor* output = nullptr;
      TF_RETURN_IF_ERROR(context->allocate_output(0, output_shape, &output));
      done();
    }

    // Search to see whether our tensor is desired by any existing batch.
    auto desire_it = desired_tensor_to_batch_map_.find(batch_key);
    if (desire_it != desired_tensor_to_batch_map_.end()) {
      // Mark our tensor as no longer missing.
      auto batch_it = available_batches_.find(desire_it->second);
      desired_tensor_to_batch_map_.erase(desire_it);
      if (batch_it == available_batches_.end()) {
        return errors::InvalidArgument("Batch no longer exists.");
      }
      batch_it->second.missing_tensors.erase(batch_key);
      // If all tensors are available we should concatenate them and dispatch
      // the batch.
      if (batch_it->second.missing_tensors.empty()) {
        TF_RETURN_IF_ERROR(
            OutputBatch(batch_it->second.context, batch_it->second.done));
        available_batches_.erase(batch_it);
      }
    }
    return Status::OK();
  }

 private:
  mutex mu_;

  // Represents a still-incomplete batch of tensors. When all tensors become
  // available they will be concatenated in the right order and sent through the
  // context.
  struct Batch {
    // Batch keys for tensors which are still missing from this batch. When this
    // is empty the Tensors can be concatenated and forwarded.
    std::unordered_set<int64> missing_tensors;

    // Context and callback for the session responsible for finishing this
    // batch.
    OpKernelContext* context;
    AsyncOpKernel::DoneCallback done;
  };

  // Map from batch key of the session which will output the batched gradients
  // to still-incomplete batches.
  std::unordered_map<int64, Batch> available_batches_;

  // Map from batch key to tensors which are waiting for their batches to be
  // available.
  std::unordered_map<int64, Tensor> available_tensors_;

  // Map from batch key of a tensor which is not yet available to the batch key
  // of the batch to which it belongs.
  std::unordered_map<int64, int64> desired_tensor_to_batch_map_;
};

class UnbatchGradKernel : public AsyncOpKernel {
 public:
  explicit UnbatchGradKernel(OpKernelConstruction* c) : AsyncOpKernel(c) {
    OP_REQUIRES_OK(c, c->GetAttr("container", &container_));
    OP_REQUIRES_OK(c, c->GetAttr("shared_name", &shared_name_));
    // If shared_name is not supplied, use name instead (prevent collisions by
    // default).
    if (shared_name_.empty()) {
      shared_name_ = name();
    }
  }

  void ComputeAsync(OpKernelContext* c, DoneCallback done) final {
    UnbatchGradResource* ubr;
    std::function<Status(UnbatchGradResource**)> creator =
        [](UnbatchGradResource** r) {
          *r = new UnbatchGradResource();
          return Status::OK();
        };
    OP_REQUIRES_OK_ASYNC(c,
                         c->resource_manager()->LookupOrCreate(
                             container_, shared_name_, &ubr, creator),
                         done);
    Status status = ubr->Compute(c, done);
    ubr->Unref();
    OP_REQUIRES_OK_ASYNC(c, status, done);
    // Assume ubr calls done, so nothing to do here.
  }

 private:
  string container_;
  string shared_name_;
};
REGISTER_KERNEL_BUILDER(Name("UnbatchGrad").Device(DEVICE_CPU),
                        UnbatchGradKernel);

}  // namespace tensorflow
