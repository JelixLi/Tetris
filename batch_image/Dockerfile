FROM registry.cn-hangzhou.aliyuncs.com/gcr_cn/serving_run:2.4.1

# Expose ports
# gRPC
EXPOSE 8500

# REST
EXPOSE 8501

# Set where models should be stored in the container
ENV MODEL_BASE_PATH=/models
RUN mkdir -p ${MODEL_BASE_PATH}

# The only required piece is the model name in order to differentiate endpoints
ENV MODEL_NAME=model

# Log level
# ENV TF_CPP_MIN_VLOG_LEVEL=3

# RUN echo -e 'max_batch_size { value: 1 }\nbatch_timeout_micros { value: 0 }\nmax_enqueued_batches { value: 1 }\nnum_batch_threads { value: 1 }' > /batching_parameters.txt
# COPY batching_parameters.txt /batching_parameters.txt

RUN echo '#!/bin/bash \n\n\
echo -e "max_batch_size { value: ${BATCH_SIZE} }\nbatch_timeout_micros { value: ${BATCH_TIMEOUT} }\nmax_enqueued_batches { value: 3 }\nnum_batch_threads { value: ${BATCH_THREADS} }" > /batching_parameters.txt \n\n \
if [ ${ENABLE_BATCH} == "true" ]; then \n\n \
    LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libtcmalloc.so.4 tensorflow_model_server --port=8500 --rest_api_port=8501 \
--model_name=${MODEL_NAME} --model_base_path=${MODEL_BASE_PATH}/${MODEL_NAME} --enable_batching=true --batching_parameters_file=/batching_parameters.txt \
"$@" \n\n \
else \n\n \
    LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libtcmalloc.so.4 tensorflow_model_server --port=8500 --rest_api_port=8501 \
--model_name=${MODEL_NAME} --model_base_path=${MODEL_BASE_PATH}/${MODEL_NAME} --enable_batching=false \
"$@"\n\n \
fi' > /usr/bin/tf_serving_entrypoint.sh \    
&& chmod +x /usr/bin/tf_serving_entrypoint.sh
# LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libtcmalloc.so.4 tensorflow_model_server --port=8500 --rest_api_port=8501 \
# --model_name=${MODEL_NAME} --model_base_path=${MODEL_BASE_PATH}/${MODEL_NAME} --enable_batching=${ENABLE_BATCH} --batching_parameters_file=/batching_parameters.txt \
# "$@"' > /usr/bin/tf_serving_entrypoint.sh \    
# && chmod +x /usr/bin/tf_serving_entrypoint.sh

ENTRYPOINT ["/usr/bin/tf_serving_entrypoint.sh"]
# ENTRYPOINT ["/bin/bash"]


# BATCH_SIZE
# BATCH_TIMEOUT
# ENABLE_BATCH
# BATCH_THREADS