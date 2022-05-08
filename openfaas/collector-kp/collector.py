import sys
import os
from kubernetes import client, config
import time
import datetime
import fcntl
# import multiprocessing

MODEL_PATH = "/models/"

COLLECT_PERIOD = 5 # s
KEEP_ALIVE = 1 # m

LOCK_PATH = "/home/tank/lijie/serving_locks/"
MEMORY_PATH = "/dev/shm/serving_memorys/"

def get_models():
    models = os.listdir(MODEL_PATH)
    return models

def get_tensors(models):
    model_tensors = {}
    for model in models:
        with open(MODEL_PATH+model) as f:
            model_tensors[model] = set(f.read().split())
    return model_tensors

def get_existing_tensors():
    tensor_set = set()
    m_files = os.listdir(MEMORY_PATH)
    for t in m_files:
        tensor_set.add(t)
    return tensor_set

def is_running(model,pods):
    for p in pods:
        if model in p:
            return True
    return False

def get_expired_tensor(delete_tensor_set,keep_alive_dict):
    expired_tensors = set()
    for t in delete_tensor_set:
        if t in keep_alive_dict:
            if keep_alive_dict[t] + datetime.timedelta(minutes=KEEP_ALIVE) <= datetime.datetime.now():
                expired_tensors.add(t)
        else:
            keep_alive_dict[t] = datetime.datetime.now()
    return expired_tensors


def delete_tensors(expired_tensor_set):
    for t in expired_tensor_set:
        lock_file = LOCK_PATH + t
        memory_file = MEMORY_PATH + t
        if os.path.exists(lock_file) and os.path.exists(memory_file):
            f = open(lock_file,"w")
            fcntl.lockf(f,fcntl.LOCK_EX) # acquire an exclusive lock
            os.remove(memory_file)
            fcntl.lockf(f,fcntl.LOCK_UN) # unlock


def node_collector(node_name):
    config.load_incluster_config()
    v1 = client.CoreV1Api()
    namespace = "openfaasdev"
    models = get_models()
    model_tensors = get_tensors(models)
    keep_alive_dict = {}
    while True:
        time.sleep(COLLECT_PERIOD)
        pod_list = v1.list_namespaced_pod(namespace)
        running_pods = [pod.metadata.name for pod in pod_list.items if pod.spec.node_name==node_name]
        running_models = [m for m in models if is_running(m,running_pods)]
        running_tensor_set = set()
        for m in running_models:
            running_tensor_set = running_tensor_set.union(model_tensors[m])
        existing_tensor_set = get_existing_tensors()
        delete_tensor_set = existing_tensor_set.difference(running_tensor_set)
        expired_tensor_set = get_expired_tensor(delete_tensor_set,keep_alive_dict)
        delete_tensors(expired_tensor_set)

def main():
    f = open("/root/hostname","r")
    node_name = f.read().strip()
    node_collector(node_name)

    # config.load_incluster_config()
    # v1 = client.CoreV1Api()
    # node_list = v1.list_node()
    # node_names = [node.name for node in node_list.items]
    # collector_processes = []
    # for node in node_names:
    #     p = multiprocessing.Process(target=node_collector, args = (node,))
    #     collector_processes.append(p)
    #     p.start()
    # for p in collector_processes:
    #     p.join()


if __name__ == '__main__':
    main()