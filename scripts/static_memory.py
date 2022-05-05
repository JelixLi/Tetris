from multiprocessing import Process
import time
import os
import subprocess
import re
from threading import Thread
import sys

MODEL_PATH = ""
MODEL_NAME = ""
IMAGE = ""

def construct_run_cmd(model_name,cpus):
    base_run_cmd = "docker run -itd --rm"
    # cpu_para = "--cpuset-cpus="+cpus
    cpu_para = ""
    # volume_para = "-v /home/tank/lijie/change_model/"+model_name+":/models/"+model_name
    volume_para = "-v " + MODEL_PATH + "/" + model_name + ":/models/" + model_name
    volume_para = volume_para + " -v /dev/shm/serving_memorys/:/dev/shm/serving_memorys/ -v /home/tank/lijie/serving_locks/:/home/tank/lijie/serving_locks/"
    model_name_para = "-e MODEL_NAME="+model_name
    # runtime_image_para = "tensorflow/serving:2.4.1"
    runtime_image_para = IMAGE
    return base_run_cmd+" "+cpu_para+" " \
                +volume_para+" "+model_name_para+" " \
                    +runtime_image_para 

def construct_stop_cmd(container_id):
    cmd = "docker stop " + container_id
    return cmd 


def construct_runtime(model_name,cpus):
    cmd = construct_run_cmd(model_name=model_name,
                            cpus=cpus)
    # print(cmd)
    print("CONSTRUCTING DOCKER CONTAINER.....")
    p = subprocess.Popen(cmd,stdout=subprocess.PIPE,shell=True)
    out,_ = p.communicate()
    p.wait()
    time.sleep(5)
    print("DOCKER CONTAINER STARTED.....")
    return out.decode()

def deconstruct_runtime(runtime):
    print("DECONSTRUCTING DOCKER CONTAINER.....")
    cmd = construct_stop_cmd(runtime)
    p = subprocess.Popen(cmd,stdout=subprocess.PIPE,shell=True)
    out,_ = p.communicate()
    p.wait()
    print("DOCKER CONTAINER STOPPED.....")
    return out.decode() 

def transform(out):
    out = ((out.split('\n')[1]).split())[3]
    while out[-1].isalpha():
        out = out[:-1]
    return out

def construct_stats_cmd(container_id):
    cmd = "docker stats --no-stream " + container_id
    return cmd 

def measure_mem_stats(runtime):
    print("MEASURING DOCKER CONTAINER MEM STATS.....")
    cmd = construct_stats_cmd(runtime)
    p = subprocess.Popen(cmd,stdout=subprocess.PIPE,shell=True)
    out,_ = p.communicate()
    p.wait()
    print("DOCKER CONTAINER MEM STATS MEASURED.....")
    return transform(out.decode()) 

assert(len(sys.argv) == 5)

MODEL_PATH = sys.argv[1]
MODEL_NAME = sys.argv[2]
IMAGE = sys.argv[3]
INSTANCE_NUM = int(sys.argv[4])

# print(MODEL_PATH)
# print(MODEL_NAME)
# print(IMAGE)
# print(INSTANCE_NUM)

MEMS = 0.0
instances = []
for i in range(INSTANCE_NUM):
    instance = construct_runtime(MODEL_NAME,"",)
    instances.append(instance)

for instance in instances:
    MEMS += float(measure_mem_stats(instance))

for instance in instances:
    deconstruct_runtime(instance)

print("model name: " + MODEL_NAME)
print("instance num: " + str(INSTANCE_NUM))
print("image: " + IMAGE)
print("total mems: " + str(MEMS))

# sudo python static_memory.py $(pwd) vgg16 tensorflow/serving:2.4.1 2
