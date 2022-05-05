FROM registry.cn-hangzhou.aliyuncs.com/gcr_cn/serving:2.4.1-devel 

WORKDIR /tensorflow-serving

# Build, and install TensorFlow Serving
ENV TF_SERVING_BUILD_OPTIONS="--config=release"
# ENV TF_SERVING_BUILD_OPTIONS="--config=nativeopt"
# ENV TF_SERVING_BUILD_OPTIONS="--config=mkl_open_source_only --config=release"
# ENV TF_SERVING_BUILD_OPTIONS="--config=mkl --config=release"
# ARG TF_SERVING_BUILD_OPTIONS="--config=mkl --config=release"
RUN echo "Building with build options: ${TF_SERVING_BUILD_OPTIONS}"
ENV TF_SERVING_BAZEL_OPTIONS=""
RUN echo "Building with Bazel options: ${TF_SERVING_BAZEL_OPTIONS}"

ENTRYPOINT bazel build --color=yes --curses=yes \
            ${TF_SERVING_BAZEL_OPTIONS} \
            --verbose_failures \
            --output_filter=DONT_MATCH_ANYTHING \
            ${TF_SERVING_BUILD_OPTIONS} \
            tensorflow_serving/model_servers:tensorflow_model_server