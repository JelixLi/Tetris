import requests
import numpy as np

# URL = "http://127.0.0.1:31118/function/resnet-50-keras/v1/models/resnet-50-keras:predict"

# URL = "http://127.0.0.1:31212/function/resnet-50-keras/v1/models/resnet-50-keras:predict"

URL = "http://127.0.0.1:31212/function/resnet-50-keras"


item = '{"input_1":%s}' % np.random.rand(224,224,3).tolist()
batch_size = 1
request_num = 1
base_str = item
predict_request = '{"instances" : [' + base_str + ']}'
for i in range(batch_size-1):
    base_str = base_str + ',' + item
    predict_request = '{"instances" : [' + base_str + ']}'


elapsed_times = []

headers = {
    "input_data_size": str(4)
}

# print(predict_request)

# predict_request='{"signature_name": "serving_default", "instances":[{"inputs":%s}] }' %np.random.rand(224,224,3).tolist()
for i in range(request_num):
    response = requests.post(url=URL, data=predict_request,headers=headers)
    response.raise_for_status()
    print(response.elapsed.total_seconds() * 1000)
    elapsed_times.append(response.elapsed.total_seconds() * 1000)
print(elapsed_times)