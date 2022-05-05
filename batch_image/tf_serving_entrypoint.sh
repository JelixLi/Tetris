#!/bin/bash \n\n\
echo -e "max_batch_size { value: ${BATCH_SIZE} }\nbatch_timeout_micros { value: ${BATCH_TIMEOUT} }\nmax_enqueued_batches { value: 3 }\nnum_batch_threads { value: ${BATCH_THREADS} }" > batching_parameters.txt && LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libtcmalloc.so.4 tensorflow_model_server --port=8500 --rest_api_port=8501 \
--model_name=${MODEL_NAME} --model_base_path=${MODEL_BASE_PATH}/${MODEL_NAME} --enable_batching=${ENABLE_BATCH} --batching_parameters_file=/batching_parameters.txt \
"$@"
