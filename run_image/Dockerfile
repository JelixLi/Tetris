FROM ubuntu:18.04

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        google-perftools \
        && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Install TF Serving pkg
COPY tensorflow_model_server /usr/bin/tensorflow_model_server

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
COPY batching_parameters.txt /batching_parameters.txt
COPY libiomp5.so /usr/local/lib

ENV LIBRARY_PATH '/usr/local/lib:$LIBRARY_PATH'
ENV LD_LIBRARY_PATH '/usr/local/lib:$LD_LIBRARY_PATH'

# Create a script that runs the model server so we can use environment variables
# while also passing in arguments from the docker command line

# RUN echo '#!/bin/bash \n\n\
# tensorflow_model_server --port=8500 --rest_api_port=8501 \
# --model_name=${MODEL_NAME} --model_base_path=${MODEL_BASE_PATH}/${MODEL_NAME} \
# "$@"' > /usr/bin/tf_serving_entrypoint.sh \
# && chmod +x /usr/bin/tf_serving_entrypoint.sh

RUN echo '#!/bin/bash \n\n\
LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libtcmalloc.so.4  tensorflow_model_server --port=8500 --rest_api_port=8501 \
--model_name=${MODEL_NAME} --model_base_path=${MODEL_BASE_PATH}/${MODEL_NAME} \
"$@"' > /usr/bin/tf_serving_entrypoint.sh \
&& chmod +x /usr/bin/tf_serving_entrypoint.sh

# RUN echo '#!/bin/bash \n\n\
# tensorflow_model_server --port=8500 --rest_api_port=8501 \
# --model_name=${MODEL_NAME} --model_base_path=${MODEL_BASE_PATH}/${MODEL_NAME} \
# "$@"' > /usr/bin/tf_serving_entrypoint.sh \
# && chmod +x /usr/bin/tf_serving_entrypoint.sh

# RUN echo '#!/bin/bash \n\n\
# LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libtcmalloc.so.4 tensorflow_model_server --port=8500 --rest_api_port=8501 \
# --model_name=${MODEL_NAME} --model_base_path=${MODEL_BASE_PATH}/${MODEL_NAME} --enable_batching=true --batching_parameters_file=/batching_parameters.txt \
# "$@"' > /usr/bin/tf_serving_entrypoint.sh \
# && chmod +x /usr/bin/tf_serving_entrypoint.sh

ENTRYPOINT ["/usr/bin/tf_serving_entrypoint.sh"]
# ENTRYPOINT ["/bin/bash"]