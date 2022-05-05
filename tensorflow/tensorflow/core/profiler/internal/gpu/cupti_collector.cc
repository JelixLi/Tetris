/* Copyright 2020 The TensorFlow Authors. All Rights Reserved.

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

#include "tensorflow/core/profiler/internal/gpu/cupti_collector.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "third_party/gpus/cuda/include/cuda.h"
#include "tensorflow/core/platform/abi.h"
#include "tensorflow/core/platform/host_info.h"
#include "tensorflow/core/platform/mutex.h"
#include "tensorflow/core/profiler/utils/parse_annotation.h"
#include "tensorflow/core/profiler/utils/xplane_builder.h"
#include "tensorflow/core/profiler/utils/xplane_schema.h"
#include "tensorflow/core/profiler/utils/xplane_utils.h"

namespace tensorflow {
namespace profiler {

namespace {

bool IsHostEvent(const CuptiTracerEvent& event) {
  // DriverCallback(i.e. kernel launching) events are host events.
  if (event.source == CuptiTracerEventSource::DriverCallback) return true;
  // Non-overhead activity events are device events.
  if (event.type != CuptiTracerEventType::Overhead) return false;
  // Overhead events can be associated with a thread or a stream, etc.
  // If a valid thread id is specified, we consider it as a host event.
  return event.thread_id != CuptiTracerEvent::kInvalidThreadId;
}

void CreateXEvent(const CuptiTracerEvent& event, XPlaneBuilder* plane,
                  uint64 start_gpu_ns, uint64 end_gpu_ns, XLineBuilder* line) {
  if (event.start_time_ns < start_gpu_ns || event.end_time_ns > end_gpu_ns ||
      event.start_time_ns > event.end_time_ns) {
    VLOG(2) << "events have abnormal timestamps:" << event.name
            << " start time(ns): " << event.start_time_ns
            << " end time(ns): " << event.end_time_ns;
    return;
  }
  std::string kernel_name = port::MaybeAbiDemangle(event.name.c_str());
  if (kernel_name.empty()) {
    kernel_name = GetTraceEventTypeName(event.type);
  }
  XEventMetadata* event_metadata =
      plane->GetOrCreateEventMetadata(std::move(kernel_name));
  XEventBuilder xevent = line->AddEvent(*event_metadata);
  xevent.SetTimestampNs(event.start_time_ns);
  xevent.SetEndTimestampNs(event.end_time_ns);
  if (event.correlation_id != CuptiTracerEvent::kInvalidCorrelationId) {
    xevent.AddStatValue(*plane->GetOrCreateStatMetadata(
                            GetStatTypeStr(StatType::kCorrelationId)),
                        event.correlation_id);
  }
  if (!event.annotation.empty()) {
    xevent.AddStatValue(*plane->GetOrCreateStatMetadata(
                            GetStatTypeStr(StatType::kKernelAnnotation)),
                        *plane->GetOrCreateStatMetadata(event.annotation));
  }
  if (event.context_id != CuptiTracerEvent::kInvalidContextId) {
    xevent.AddStatValue(
        *plane->GetOrCreateStatMetadata(GetStatTypeStr(StatType::kContextId)),
        absl::StrCat("$$", static_cast<uint64>(event.context_id)));
  }
  if (event.type == CuptiTracerEventType::Kernel) {
    std::string kernel_details = absl::StrCat(
        "regs:", event.kernel_info.registers_per_thread,
        " shm:", event.kernel_info.static_shared_memory_usage,
        " grid:", event.kernel_info.grid_x, ",", event.kernel_info.grid_y, ",",
        event.kernel_info.grid_z, " block:", event.kernel_info.block_x, ",",
        event.kernel_info.block_y, ",", event.kernel_info.block_z);
    xevent.AddStatValue(*plane->GetOrCreateStatMetadata(
                            GetStatTypeStr(StatType::kKernelDetails)),
                        *plane->GetOrCreateStatMetadata(kernel_details));
  } else if (event.type == CuptiTracerEventType::MemcpyH2D ||
             event.type == CuptiTracerEventType::MemcpyD2H ||
             event.type == CuptiTracerEventType::MemcpyD2D ||
             event.type == CuptiTracerEventType::MemcpyP2P ||
             event.type == CuptiTracerEventType::MemcpyOther) {
    const auto& memcpy_info = event.memcpy_info;
    std::string memcpy_details = absl::StrCat("size:", memcpy_info.num_bytes,
                                              " dest:", memcpy_info.destination,
                                              " async:", memcpy_info.async);
    xevent.AddStatValue(*plane->GetOrCreateStatMetadata(
                            GetStatTypeStr(StatType::kMemcpyDetails)),
                        memcpy_details);
  } else if (event.type == CuptiTracerEventType::MemoryAlloc) {
    std::string memalloc_details =
        absl::StrCat("num_bytes:", event.memalloc_info.num_bytes);
    xevent.AddStatValue(*plane->GetOrCreateStatMetadata(
                            GetStatTypeStr(StatType::kMemallocDetails)),
                        memalloc_details);
  }

  std::vector<Annotation> annotation_stack =
      ParseAnnotationStack(event.annotation);
  // If multiple metadata have the same key name, show the values from the top
  // of the stack (innermost annotation). Concatenate the values from "hlo_op".
  absl::flat_hash_set<absl::string_view> key_set;
  std::vector<absl::string_view> hlo_op_names;
  for (auto annotation = annotation_stack.rbegin();
       annotation != annotation_stack.rend(); ++annotation) {
    for (const Annotation::Metadata& metadata : annotation->metadata) {
      if (metadata.key == "tf_op") {
        continue;  // ignored, obtained from HLO proto via DebugInfoMap
      } else if (key_set.insert(metadata.key).second) {
        xevent.ParseAndAddStatValue(
            *plane->GetOrCreateStatMetadata(metadata.key), metadata.value);
      }
    }
  }
  // TODO(profiler): we should get rid of kLevel0, it is based on the assumption
  // that those op-related ScopedAnnotation are at the very TOP level.
  if (!annotation_stack.empty()) {
    xevent.AddStatValue(
        *plane->GetOrCreateStatMetadata(GetStatTypeStr(StatType::kLevel0)),
        *plane->GetOrCreateStatMetadata(annotation_stack.begin()->name));
  }
}

absl::optional<int> GetDeviceAttribute(CUdevice device,
                                       CUdevice_attribute attrib) {
  int ret_val;
  CUresult err = cuDeviceGetAttribute(&ret_val, attrib, device);
  if (err != CUDA_SUCCESS) return absl::nullopt;
  return ret_val;
}

std::string GetDeviceXLineName(
    int64 stream_id, absl::flat_hash_set<CuptiTracerEventType>& event_types) {
  std::string line_name = absl::StrCat("Stream #", stream_id);
  event_types.erase(CuptiTracerEventType::Unsupported);
  if (event_types.empty()) return line_name;
  std::vector<const char*> type_names;
  for (const auto event_type : event_types) {
    type_names.emplace_back(GetTraceEventTypeName(event_type));
  }
  return absl::StrCat(line_name, "(", absl::StrJoin(type_names, ","), ")");
}

}  // namespace

void AnnotationMap::Add(uint32 device_id, uint32 correlation_id,
                        const std::string& annotation) {
  if (annotation.empty()) return;
  VLOG(3) << "Add annotation: device_id: " << device_id
          << " correlation_id: " << correlation_id
          << " annotation: " << annotation;
  if (device_id >= per_device_map_.size()) return;
  auto& per_device_map = per_device_map_[device_id];
  absl::MutexLock lock(&per_device_map.mutex);
  if (per_device_map.annotations.size() < max_size_) {
    absl::string_view annotation_str =
        *per_device_map.annotations.insert(annotation).first;
    per_device_map.correlation_map.emplace(correlation_id, annotation_str);
  }
}

absl::string_view AnnotationMap::LookUp(uint32 device_id,
                                        uint32 correlation_id) {
  if (device_id >= per_device_map_.size()) return absl::string_view();
  auto& per_device_map = per_device_map_[device_id];
  absl::MutexLock lock(&per_device_map.mutex);
  auto it = per_device_map.correlation_map.find(correlation_id);
  return it != per_device_map.correlation_map.end() ? it->second
                                                    : absl::string_view();
}

// CuptiTraceCollectorImpl store the CuptiTracerEvents from CuptiTracer and
// eventually convert and filter them to StepStats or XSpace.
class CuptiTraceCollectorImpl : public CuptiTraceCollector {
 public:
  CuptiTraceCollectorImpl(const CuptiTracerCollectorOptions& option,
                          uint64 start_walltime_ns, uint64 start_gpu_ns)
      : CuptiTraceCollector(option),
        num_callback_events_(0),
        num_activity_events_(0),
        start_walltime_ns_(start_walltime_ns),
        start_gpu_ns_(start_gpu_ns),
        num_gpus_(option.num_gpus),
        per_device_collector_(option.num_gpus) {}

  void AddEvent(CuptiTracerEvent&& event) override {
    if (event.device_id >= num_gpus_) return;
    if (event.source == CuptiTracerEventSource::DriverCallback) {
      if (num_callback_events_ > options_.max_callback_api_events) {
        OnEventsDropped("total driver(callback) events reaches max", 1);
        return;
      }
      num_callback_events_++;
    } else {
      if (num_activity_events_ > options_.max_activity_api_events) {
        OnEventsDropped("total device(activity) events reaches max", 1);
        return;
      }
      num_activity_events_++;
    }
    per_device_collector_[event.device_id].AddEvent(std::move(event));
  }
  void OnEventsDropped(const std::string& reason, uint32 num_events) override {
    absl::MutexLock lock(&mutex_);
    dropped_events_[reason] += num_events;
  }
  void Flush() override {}
  void Export(StepStats* step_stats) override {
    LOG(INFO) << " GpuTracer has collected " << num_callback_events_
              << " callback api events and " << num_activity_events_
              << " activity events. " << ReportDroppedEvents();
    for (int i = 0; i < num_gpus_; ++i) {
      per_device_collector_[i].Flush(i, start_walltime_ns_, start_gpu_ns_,
                                     step_stats);
    }
  }
  void Export(XSpace* space, uint64 end_gpu_ns) override {
    LOG(INFO) << " GpuTracer has collected " << num_callback_events_
              << " callback api events and " << num_activity_events_
              << " activity events. " << ReportDroppedEvents();
    XPlaneBuilder host_plane(
        FindOrAddMutablePlaneWithName(space, kCuptiDriverApiPlaneName));
    for (int device_ordinal = 0; device_ordinal < num_gpus_; ++device_ordinal) {
      std::string name = GpuPlaneName(device_ordinal);
      XPlaneBuilder device_plane(FindOrAddMutablePlaneWithName(space, name));
      device_plane.SetId(device_ordinal);
      per_device_collector_[device_ordinal].Flush(start_gpu_ns_, end_gpu_ns,
                                                  &device_plane, &host_plane);
      per_device_collector_[device_ordinal].GetDeviceCapabilities(
          device_ordinal, &device_plane);
      NormalizeTimeStamps(&device_plane, start_walltime_ns_);
    }
    NormalizeTimeStamps(&host_plane, start_walltime_ns_);
  }
  std::string ReportDroppedEvents() {
    absl::MutexLock lock(&mutex_);
    string result;
    for (const auto& dropped : dropped_events_) {
      absl::StrAppend(&result, " ", dropped.second, " events dropped because ",
                      dropped.first, ";");
    }
    if (!result.empty()) result.back() = '.';
    return result;
  }
  std::string ReportNumEventsIfDropped() override {
    std::string events_dropped = ReportDroppedEvents();
    if (events_dropped.empty()) return "";
    return absl::StrCat("Detected GPU events dropped on ", port::Hostname(),
                        ": Profiler has collected ",
                        num_callback_events_.load(), " driver events and ",
                        num_activity_events_.load(), " device events.",
                        events_dropped);
  }

 private:
  std::atomic<int> num_callback_events_;
  std::atomic<int> num_activity_events_;
  absl::Mutex mutex_;
  absl::flat_hash_map<std::string, uint64> dropped_events_
      ABSL_GUARDED_BY(mutex_);
  uint64 start_walltime_ns_;
  uint64 start_gpu_ns_;
  int num_gpus_;

  // Set the all XLines of specified XPlane to starting walltime.
  // Events time in both host and device planes are CUTPI timestamps.
  // We set initial CUPTI timestamp as start time for all lines to reflect
  // this fact. Eventually we change line start time to corresponding
  // start_walltime_ns to normalize with CPU wall time.
  static void NormalizeTimeStamps(XPlaneBuilder* plane,
                                  uint64 start_walltime_ns) {
    plane->ForEachLine(
        [&](XLineBuilder line) { line.SetTimestampNs(start_walltime_ns); });
  }

  struct CorrelationInfo {
    CorrelationInfo(uint32 t, uint32 e) : thread_id(t), enqueue_time_ns(e) {}
    uint32 thread_id;
    uint64 enqueue_time_ns;
  };
  struct PerDeviceCollector {
    void AddEvent(CuptiTracerEvent&& event) {
      mutex_lock l(m);
      if (event.source == CuptiTracerEventSource::DriverCallback) {
        // Cupti api callback events were used to populate launch times etc.
        if (event.correlation_id != CuptiTracerEvent::kInvalidCorrelationId) {
          correlation_info.insert(
              {event.correlation_id,
               CorrelationInfo(event.thread_id, event.start_time_ns)});
        }
        events.emplace_back(std::move(event));
      } else {
        // Cupti activity events measure device times etc.
        events.emplace_back(std::move(event));
      }
    }

    void Flush(int32 device_ordinal, uint64 start_walltime_ns,
               uint64 start_gpu_ns, StepStats* step_stats) {
      mutex_lock l(m);
      absl::flat_hash_map<std::pair<int64 /*stream_id*/, CuptiTracerEventType>,
                          DeviceStepStats*>
          stream_dev_stats_map;
      DeviceStepStats* unknown_stream_dev_stats = nullptr;
      DeviceStepStats* all_streams_dev_stats = nullptr;
      DeviceStepStats* memcpy_dev_stats = nullptr;
      DeviceStepStats* sync_dev_stats = nullptr;
      for (const CuptiTracerEvent& event : events) {
        NodeExecStats* ns = new NodeExecStats;
        ns->set_all_start_micros(
            (start_walltime_ns + (event.start_time_ns - start_gpu_ns)) / 1000);
        ns->set_op_start_rel_micros(0);
        auto elapsed_ns = event.end_time_ns - event.start_time_ns;
        ns->set_op_end_rel_micros(elapsed_ns / 1000);
        ns->set_all_end_rel_micros(elapsed_ns / 1000);

        if (event.source == CuptiTracerEventSource::DriverCallback) {
          // Legacy code ignore all other launch events except
          // cuStreamSynchronize.
          if (event.name == "cuStreamSynchronize") {
            ns->set_node_name(event.name);
            ns->set_timeline_label(absl::StrCat("ThreadId ", event.thread_id));
            ns->set_thread_id(event.thread_id);
            if (sync_dev_stats == nullptr) {
              sync_dev_stats = step_stats->add_dev_stats();
              sync_dev_stats->set_device(
                  absl::StrCat("/device:GPU:", device_ordinal, "/sync"));
            }
            sync_dev_stats->add_node_stats()->Swap(ns);
          }
        } else {  // CuptiTracerEventSource::Activity
          // Get launch information if available.
          if (event.correlation_id != CuptiTracerEvent::kInvalidCorrelationId) {
            auto it = correlation_info.find(event.correlation_id);
            if (it != correlation_info.end()) {
              ns->set_scheduled_micros(it->second.enqueue_time_ns / 1000);
              ns->set_thread_id(it->second.thread_id);
            }
          }

          auto annotation_stack = ParseAnnotationStack(event.annotation);
          std::string kernel_name = port::MaybeAbiDemangle(event.name.c_str());
          std::string activity_name =
              !annotation_stack.empty()
                  ? std::string(annotation_stack.back().name)
                  : kernel_name;
          ns->set_node_name(activity_name);
          switch (event.type) {
            case CuptiTracerEventType::Kernel: {
              ns->set_timeline_label(absl::StrCat(
                  kernel_name, " regs:", event.kernel_info.registers_per_thread,
                  " shm:", event.kernel_info.static_shared_memory_usage,
                  " grid: ", event.kernel_info.grid_x, ",",
                  event.kernel_info.grid_y, ",", event.kernel_info.grid_z,
                  " block:", event.kernel_info.block_x, ",",
                  event.kernel_info.block_y, ",", event.kernel_info.block_z,
                  "@@", event.annotation));
              DeviceStepStats*& stream_dev_stats =
                  stream_dev_stats_map[std::make_pair(event.stream_id,
                                                      event.type)];
              if (stream_dev_stats == nullptr) {
                stream_dev_stats = step_stats->add_dev_stats();
                stream_dev_stats->set_device(
                    absl::StrCat("/device:GPU:", device_ordinal,
                                 "/stream:", event.stream_id));
              }
              *stream_dev_stats->add_node_stats() = *ns;
              if (all_streams_dev_stats == nullptr) {
                all_streams_dev_stats = step_stats->add_dev_stats();
                all_streams_dev_stats->set_device(absl::StrCat(
                    "/device:GPU:", device_ordinal, "/stream:all"));
              }
              all_streams_dev_stats->add_node_stats()->Swap(ns);
              break;
            }
            case CuptiTracerEventType::MemcpyH2D:
            case CuptiTracerEventType::MemcpyD2H:
            case CuptiTracerEventType::MemcpyD2D:
            case CuptiTracerEventType::MemcpyP2P: {
              std::string details = absl::StrCat(
                  activity_name, " bytes:", event.memcpy_info.num_bytes);
              if (event.memcpy_info.async) {
                absl::StrAppend(&details, " aync");
              }
              if (event.memcpy_info.destination != event.device_id) {
                absl::StrAppend(&details,
                                " to device:", event.memcpy_info.destination);
              }
              ns->set_timeline_label(std::move(details));
              DeviceStepStats*& stream_dev_stats =
                  stream_dev_stats_map[std::make_pair(event.stream_id,
                                                      event.type)];
              if (stream_dev_stats == nullptr) {
                stream_dev_stats = step_stats->add_dev_stats();
                stream_dev_stats->set_device(absl::StrCat(
                    "/device:GPU:", device_ordinal, "/stream:", event.stream_id,
                    "<", GetTraceEventTypeName(event.type), ">"));
              }
              *stream_dev_stats->add_node_stats() = *ns;
              if (memcpy_dev_stats == nullptr) {
                memcpy_dev_stats = step_stats->add_dev_stats();
                memcpy_dev_stats->set_device(
                    absl::StrCat("/device:GPU:", device_ordinal, "/memcpy"));
              }
              memcpy_dev_stats->add_node_stats()->Swap(ns);
              break;
            }
            default:
              ns->set_timeline_label(activity_name);
              if (unknown_stream_dev_stats == nullptr) {
                unknown_stream_dev_stats = step_stats->add_dev_stats();
                unknown_stream_dev_stats->set_device(
                    absl::StrCat("/device:GPU:", device_ordinal, "/stream:"));
              }
              unknown_stream_dev_stats->add_node_stats()->Swap(ns);
              break;
          }
        }
      }
      events.clear();
    }

    void Flush(uint64 start_gpu_ns, uint64 end_gpu_ns,
               XPlaneBuilder* device_plane, XPlaneBuilder* host_plane) {
      mutex_lock l(m);
      // Tracking event types per line.
      absl::flat_hash_map<int64, absl::flat_hash_set<CuptiTracerEventType>>
          events_types_per_line;
      for (auto& event : events) {
        bool is_host_event = IsHostEvent(event);
        int64 line_id = is_host_event ? static_cast<int64>(event.thread_id)
                                      : event.stream_id;
        if (line_id == CuptiTracerEvent::kInvalidThreadId ||
            line_id == CuptiTracerEvent::kInvalidStreamId)
          continue;
        auto* plane = is_host_event ? host_plane : device_plane;
        XLineBuilder line = plane->GetOrCreateLine(line_id);
        line.SetTimestampNs(start_gpu_ns);
        CreateXEvent(event, plane, start_gpu_ns, end_gpu_ns, &line);
        events_types_per_line[line_id].emplace(event.type);
      }
      device_plane->ForEachLine([&](XLineBuilder line) {
        line.SetName(
            GetDeviceXLineName(line.Id(), events_types_per_line[line.Id()]));
      });
      host_plane->ForEachLine([&](XLineBuilder line) {
        line.SetName(absl::StrCat("Host Threads/", line.Id()));
      });
      events.clear();
    }

    void GetDeviceCapabilities(int32 device_ordinal,
                               XPlaneBuilder* device_plane) {
      CUdevice device;
      if (cuDeviceGet(&device, device_ordinal) != CUDA_SUCCESS) return;

      auto clock_rate_in_khz =
          GetDeviceAttribute(device, CU_DEVICE_ATTRIBUTE_CLOCK_RATE);
      if (clock_rate_in_khz) {
        device_plane->AddStatValue(
            *device_plane->GetOrCreateStatMetadata(
                GetStatTypeStr(StatType::kDevCapClockRateKHz)),
            *clock_rate_in_khz);
      }

      auto core_count =
          GetDeviceAttribute(device, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT);
      if (core_count) {
        device_plane->AddStatValue(
            *device_plane->GetOrCreateStatMetadata(
                GetStatTypeStr(StatType::kDevCapCoreCount)),
            *core_count);
      }

      auto mem_clock_khz =
          GetDeviceAttribute(device, CU_DEVICE_ATTRIBUTE_MEMORY_CLOCK_RATE);
      auto mem_bus_width_bits = GetDeviceAttribute(
          device, CU_DEVICE_ATTRIBUTE_GLOBAL_MEMORY_BUS_WIDTH);
      if (mem_clock_khz && mem_bus_width_bits) {
        // Times 2 because HBM is DDR memory; it gets two data bits per each
        // data lane.
        auto memory_bandwidth =
            uint64{2} * (*mem_clock_khz) * 1000 * (*mem_bus_width_bits) / 8;
        device_plane->AddStatValue(
            *device_plane->GetOrCreateStatMetadata(
                GetStatTypeStr(StatType::kDevCapMemoryBandwidth)),
            memory_bandwidth);
      }

      size_t total_memory = 0;
      if (cuDeviceTotalMem(&total_memory, device) == CUDA_SUCCESS) {
        device_plane->AddStatValue(
            *device_plane->GetOrCreateStatMetadata(
                GetStatTypeStr(StatType::kDevCapMemorySize)),
            static_cast<uint64>(total_memory));
      }

      auto compute_capability_major = GetDeviceAttribute(
          device, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR);
      if (compute_capability_major) {
        device_plane->AddStatValue(
            *device_plane->GetOrCreateStatMetadata(
                GetStatTypeStr(StatType::kDevCapComputeCapMajor)),
            *compute_capability_major);
      }
      auto compute_capability_minor = GetDeviceAttribute(
          device, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR);
      if (compute_capability_minor) {
        device_plane->AddStatValue(
            *device_plane->GetOrCreateStatMetadata(
                GetStatTypeStr(StatType::kDevCapComputeCapMinor)),
            *compute_capability_minor);
      }
    }

    mutex m;
    std::vector<CuptiTracerEvent> events TF_GUARDED_BY(m);
    absl::flat_hash_map<uint32, CorrelationInfo> correlation_info
        TF_GUARDED_BY(m);
  };
  absl::FixedArray<PerDeviceCollector> per_device_collector_;

  TF_DISALLOW_COPY_AND_ASSIGN(CuptiTraceCollectorImpl);
};

std::unique_ptr<CuptiTraceCollector> CreateCuptiCollector(
    const CuptiTracerCollectorOptions& options, const uint64 start_walltime_ns,
    const uint64 start_gputime_ns) {
  return absl::make_unique<CuptiTraceCollectorImpl>(options, start_walltime_ns,
                                                    start_gputime_ns);
}

}  // namespace profiler
}  // namespace tensorflow
