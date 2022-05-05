#!/usr/bin/env bash
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
#
# Tests the microcontroller code using native x86 execution.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR=${SCRIPT_DIR}/../../../../..
cd "${ROOT_DIR}"

source tensorflow/lite/micro/tools/ci_build/helper_functions.sh

readable_run make -f tensorflow/lite/micro/tools/make/Makefile clean

# TODO(b/143715361): downloading first to allow for parallel builds.
readable_run make -f tensorflow/lite/micro/tools/make/Makefile third_party_downloads

# Next, build w/o TF_LITE_STATIC_MEMORY to catch additional build errors.
readable_run make -f tensorflow/lite/micro/tools/make/Makefile clean
readable_run make -j8 -f tensorflow/lite/micro/tools/make/Makefile BUILD_TYPE=no_tf_lite_static_memory build

# Next, make sure that the release build succeeds.
readable_run make -f tensorflow/lite/micro/tools/make/Makefile clean
readable_run make -j8 -f tensorflow/lite/micro/tools/make/Makefile BUILD_TYPE=release build

# Next, build with TAGS=posix. See b/170223867 for more details.
# TODO(b/168123200): Since the benchmarks can not currently be run as a test, we
# only build with TAGS=posix. Once benchmarks can be run (and are added to make
# test) we should switch to running all the tests with TAGS=posix.
readable_run make -f tensorflow/lite/micro/tools/make/Makefile clean
readable_run make -s -j8 -f tensorflow/lite/micro/tools/make/Makefile build TAGS=posix

# Next, build w/o release so that we can run the tests and get additional
# debugging info on failures.
readable_run make -f tensorflow/lite/micro/tools/make/Makefile clean
readable_run make -s -j8 -f tensorflow/lite/micro/tools/make/Makefile test

if [[ ${1} != "PRESUBMIT" ]]; then
  # Most of TFLM external contributors only use make. We are building a subset of
  # targets with bazel as part of this script to make it easier for external
  # contributors to fix these errors prior to creating a pull request.
  #
  # We only run the bazel command when this script is run locally (i.e. not via
  # test_all.sh) to avoid duplicate work on the CI system and also avoid
  # installing bazel on the TFLM Docker image.
  readable_run bazel build tensorflow/lite/micro:all
fi
