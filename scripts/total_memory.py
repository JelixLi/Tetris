import subprocess
import time
import datetime

def get_current_running_containers():
    cmd = "docker ps | grep serving | awk '{print $1}'"
    p = subprocess.Popen(cmd,stdout=subprocess.PIPE,shell=True)
    out,_ = p.communicate()
    p.wait()
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
    cmd = construct_stats_cmd(runtime)
    p = subprocess.Popen(cmd,stdout=subprocess.PIPE,shell=True)
    out,_ = p.communicate()
    p.wait()
    return transform(out.decode()) 

PERIOD = 10
OUTPUT_FILE = "output.txt"
while True:
    running_containers = get_current_running_containers().split()
    total_memory = 0.0 
    # current_time = time.time()
    current_time = datetime.datetime.now()
    for container in running_containers:
        if len(container) > 0:
            total_memory += float(measure_mem_stats(container))
    print(str(current_time) + " " + str(total_memory))
    with open(OUTPUT_FILE,"a") as f:
        f.write(str(current_time) + " " + str(total_memory) + "\n")
        f.flush()
    time.sleep(5)

