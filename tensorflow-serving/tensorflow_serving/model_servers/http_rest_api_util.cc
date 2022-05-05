/* Copyright 2020 Google Inc. All Rights Reserved.

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

#include "tensorflow_serving/model_servers/http_rest_api_util.h"

#include "google/protobuf/util/json_util.h"
#include "absl/strings/numbers.h"
#include "tensorflow/core/lib/core/errors.h"
#include "tensorflow/core/platform/errors.h"

namespace tensorflow {
namespace serving {

const char* const kPredictionApiGegex =
    R"((?i)/v1/models/([^/:]+)(?:(?:/versions/(\d+))|(?:/labels/(\w+)))?:(classify|regress|predict))";
const char* const kModelStatusApiRegex =
    R"((?i)/v1/models(?:/([^/:]+))?(?:(?:/versions/(\d+))|(?:/labels/(\w+)))?(?:\/(metadata))?)";

void AddHeaders(std::vector<std::pair<string, string>>* headers) {
  headers->push_back({"Content-Type", "application/json"});
}

Status FillModelSpecWithNameVersionAndLabel(
    const absl::string_view model_name,
    const absl::optional<int64>& model_version,
    const absl::optional<absl::string_view> model_version_label,
    ::tensorflow::serving::ModelSpec* model_spec) {
  model_spec->set_name(string(model_name));

  if (model_version.has_value() && model_version_label.has_value()) {
    return errors::InvalidArgument(
        "Both model version (", model_version.value(),
        ") and model version label (", model_version_label.value(),
        ") cannot be supplied.");
  }

  if (model_version.has_value()) {
    model_spec->mutable_version()->set_value(model_version.value());
  }
  if (model_version_label.has_value()) {
    model_spec->set_version_label(string(model_version_label.value()));
  }
  return Status::OK();
}

Status ParseModelInfo(const absl::string_view http_method,
                      const absl::string_view request_path, string* model_name,
                      absl::optional<int64>* model_version,
                      absl::optional<string>* model_version_label,
                      string* method, string* model_subresource,
                      bool* parse_successful) {
  string model_version_str;
  string model_version_label_str;
  // Parse request parameters
  if (http_method == "POST") {
    *parse_successful =
        RE2::FullMatch(string(request_path), kPredictionApiGegex, model_name,
                       &model_version_str, &model_version_label_str, method);
  } else if (http_method == "GET") {
    *parse_successful = RE2::FullMatch(
        string(request_path), kModelStatusApiRegex, model_name,
        &model_version_str, &model_version_label_str, model_subresource);
  }

  if (!model_version_str.empty()) {
    int64 version;
    if (!absl::SimpleAtoi(model_version_str, &version)) {
      return errors::InvalidArgument(
          "Failed to convert version: ", model_version_str, " to numeric.");
    }
    *model_version = version;
  }
  if (!model_version_label_str.empty()) {
    *model_version_label = model_version_label_str;
  }
  return Status::OK();
}

Status ToJsonString(const GetModelStatusResponse& response, string* output) {
  google::protobuf::util::JsonPrintOptions opts;
  opts.add_whitespace = true;
  opts.always_print_primitive_fields = true;
  // Note this is protobuf::util::Status (not TF Status) object.
  const auto& status = MessageToJsonString(response, output, opts);
  if (!status.ok()) {
    return errors::Internal("Failed to convert proto to json. Error: ",
                            status.ToString());
  }
  return Status::OK();
}

}  // namespace serving
}  // namespace tensorflow
