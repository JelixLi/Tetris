import requests
import numpy
import json 


def get_instances(batch_size):
    instance = {"input":[1703,1260,980,101,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],"sen_len":4,"dropout_keep_prob":[1.0]}
    batched_instances = []
    for i in range(batch_size):
        batched_instances.append(instance)
    return batched_instances

def do_requests_batch(request_num,batch_size):
    SERVER_URL = 'http://127.0.0.1:31118/function/lstm-maxclass-2365/v1/models/lstm-maxclass-2365:predict'
    total_time = 0.0
    tmp_data = json.dumps({"signature_name":"prediction","instances": get_instances(batch_size)})
    for i in range(request_num):
        response = requests.post(SERVER_URL, data=tmp_data)
        response.raise_for_status()
        # print(response.elapsed.total_seconds()*1000)
        total_time = total_time + response.elapsed.total_seconds() * 1000
    return total_time / request_num

 do_requests_batch(1,1)