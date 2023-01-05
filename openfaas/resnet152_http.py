import requests
import numpy as np

URL = "http://127.0.0.1:31212/function/resnet-152-keras"

item = '{"input_1":%s}' % np.random.rand(224,224,3).tolist()
batch_size = 1
request_num = 10
base_str = item
predict_request = '{"instances" : [' + base_str + ']}'
for i in range(batch_size-1):
    base_str = base_str + ',' + item
    predict_request = '{"instances" : [' + base_str + ']}'

# predict_request='{"signature_name": "serving_default", "instances":[{"inputs":%s}] }' %np.random.rand(224,224,3).tolist()
for i in range(request_num):
    response = requests.post(url=URL, data=predict_request)
    response.raise_for_status()
    print(response.elapsed.total_seconds() * 1000)

# signature_def['serving_default']:
#   The given SavedModel SignatureDef contains the following input(s):
#     inputs['input_1'] tensor_info:
#         dtype: DT_FLOAT
#         shape: (-1, 224, 224, 3)
#         name: serving_default_input_1:0
#   The given SavedModel SignatureDef contains the following output(s):
#     outputs['predictions'] tensor_info:
#         dtype: DT_FLOAT
#         shape: (-1, 1000)
#         name: StatefulPartitionedCall:0
#   Method name is: tensorflow/serving/predict