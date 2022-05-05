# Tetris
Tetris is a memory-efficient serverless inference system that automatically shares tensors across functions and instances.

# Environments
OS: Ubuntu 16.04

Kernel: Linux 4.15.0-142-generic

Docker: v20.10.1

Kubernetes: v1.20.0

Tensorflow Serving: v2.4.1

*The /dev/shm should be mounted as a tmpfs.*

# Quick Start
For quick experimentation, we pre-compiled an image of the Agent in Tetris, which can be installed by following instructions.
```
$ sudo docker pull registry.cn-hangzhou.aliyuncs.com/gcr_cn/serving_run:2.4.1-ws

2.4.1-ws: Pulling from gcr_cn/serving_run
Digest: sha256:86fd4070c57ebebff870967d68e888d02e4d8751031333b1045bc8ed34dde7bc
Status: Image is up to date for registry.cn-hangzhou.aliyuncs.com/gcr_cn/serving_run:2.4.1-ws
registry.cn-hangzhou.aliyuncs.com/gcr_cn/serving_run:2.4.1-ws
```
Then download vgg16 model from [models](https://github.com/JelixLi/Tetris/tree/main/models).
```
$ tree vgg16/
vgg16/
└── 1
    ├── assets
    ├── saved_model.pb
    └── variables
        ├── variables.data-00000-of-00001
        └── variables.index

3 directories, 3 files

$ du -sh vgg16/
529M    vgg16/
```
Create essential directories.
```
$ sudo mkdir -p /dev/shm/serving_memorys/
$ sudo mkdir -p /home/tank/lijie/serving_locks/
```
Then start the first container and stat the memory usage.
```
$ sudo docker run -itd --rm --name=vgg16 -v /dev/shm/serving_memorys/:/dev/shm/serving_memorys/ -v /home/tank/lijie/serving_locks/:/home/tank/lijie/serving_locks/ -v  $(pwd)/vgg16:/models/vgg16 -e MODEL_NAME=vgg16 registry.cn-hangzhou.aliyuncs.com/gcr_cn/serving_run:2.4.1-ws

2cd48ade450e679288f3aa51b73e5c76f3addc86a26df5a816ceda42c30a7aff

$ sudo docker stats 2cd48ade450e679288f3aa51b73e5c76f3addc86a26df5a816ceda42c30a7aff

CONTAINER ID   NAME      CPU %     MEM USAGE / LIMIT   MEM %     NET I/O       BLOCK I/O     PIDS
2cd48ade450e   vgg16     0.04%     567MiB / 125.6GiB   0.44%     2.83kB / 0B   9.07MB / 0B   197
```
Then start the second container and stat the memory usage.
```
$ sudo docker run -itd --rm --name=vgg16-1 -v /dev/shm/serving_memorys/:/dev/shm/serving_memorys/ -v /home/tank/lijie/serving_locks/:/home/tank/lijie/serving_locks/ -v  $(pwd)/vgg16:/models/vgg16 -e MODEL_NAME=vgg16 registry.cn-hangzhou.aliyuncs.com/gcr_cn/serving_run:2.4.1-ws

e06c701f47ecf9be5897b86abbb636dc97b326a8d6e4a12246c3209076887b43

$ sudo docker stats e06c701f47ecf9be5897b86abbb636dc97b326a8d6e4a12246c3209076887b43

CONTAINER ID   NAME      CPU %     MEM USAGE / LIMIT     MEM %     NET I/O       BLOCK I/O   PIDS
e06c701f47ec   vgg16-1   0.04%     36.27MiB / 125.6GiB   0.03%     2.83kB / 0B   0B / 0B     197
```
It is obvious that the tensor memorys are shared between containers. And the shared tensors could be seen in /dev/shm/serving_memorys.
```
$ ls /dev/shm/serving_memorys/

1045487101  1616260386  2479248152  3217990765  3740489978  657292230
1097585631  1700825667  2596981135  3372614657  3987628681  734911628
110664896   1850037554  2713926824  3463331143  4029309363  983211744
1263337643  185207681   303122266   3547911424  4147780861
1585725063  198601304   3058450704  3731869960  479411356
1604344674  2000790521  3100178750  3734658635  547595457
```
# Agent Evaluation
We have provided [scripts](https://github.com/JelixLi/Tetris/tree/main/scripts) for measuring agent memory consumption and model loading time.

static_memory.py measures multiple instance total mmeory consumption, here only the static memory (no requests have been processed) is measured for simplicity and quick evaluation.

For example, the following instruction measures 2 vgg16 instances' total static memory consumption with original tensorflow/serving, which should be replaced by registry.cn-hangzhou.aliyuncs.com/gcr_cn/serving_run:2.4.1-ws to run Tetris' agent.
```
$ sudo python static_memory.py $(pwd) vgg16 tensorflow/serving:2.4.1 2

CONSTRUCTING DOCKER CONTAINER.....
DOCKER CONTAINER STARTED.....
CONSTRUCTING DOCKER CONTAINER.....
DOCKER CONTAINER STARTED.....
MEASURING DOCKER CONTAINER MEM STATS.....
DOCKER CONTAINER MEM STATS MEASURED.....
MEASURING DOCKER CONTAINER MEM STATS.....
DOCKER CONTAINER MEM STATS MEASURED.....
DECONSTRUCTING DOCKER CONTAINER.....
DOCKER CONTAINER STOPPED.....
DECONSTRUCTING DOCKER CONTAINER.....
DOCKER CONTAINER STOPPED.....
model name: vgg16
instance num: 2
image: tensorflow/serving:2.4.1
total mems: 1152.7
```
time_measure.py measures the instance model loading time, with examples showing below. Where Tetris' Agent could benefit from previously launched instances.
```
$ sudo docker logs c4e527f25017d9b47630cb9b072a5349ae9ee8797f5f99511c690ac4c50de65a | python time_measure.py 

Took 96265 microseconds
```
# Overall Evaluation
Tetris is built upon a customized version of Openfaas tailored specifically for inference.

## Build faas-cli
faas-cli is a command tool for deploying functions. And the instructions for building faas-cli is available [here](https://github.com/JelixLi/Tetris/tree/main/openfaas/faas-cli#readme). And for simplicity, we also provided an already compiled version 
[here](https://github.com/JelixLi/Tetris/tree/main/openfaas/faas-cli#readme).
