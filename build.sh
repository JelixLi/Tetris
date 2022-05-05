chown -R root:root bazel/
chown -R root:root tensorflow-serving/
docker run --rm -v $(pwd)/bazel:/root/.cache/bazel -v $(pwd)/tensorflow-serving:/tensorflow-serving serving_build:2.4.1
cp $(pwd)/bazel/_bazel_root/e53bbb0b0da4e26d24b415310219b953/execroot/tf_serving/bazel-out/k8-opt/bin/tensorflow_serving/model_servers/tensorflow_model_server $(pwd)/run_image
cp $(pwd)/bazel/_bazel_root/e53bbb0b0da4e26d24b415310219b953/execroot/tf_serving/bazel-out/k8-opt/bin/external/llvm_openmp/libiomp5.so $(pwd)/run_image
cd run_image/
docker build -t serving_run:2.4.1 --no-cache .
rm tensorflow_model_server
rm libiomp5.so
cd ../
chown -R tank:tank bazel/
chown -R tank:tank tensorflow-serving/