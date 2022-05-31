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
Tetris is built upon a customized version of Openfaas tailored specifically for inference. And as it's hard to run a compelete 8-node cluster experiment, we provided a simple guide below to run a quick single-node evaluation.

## Build faas-cli
faas-cli is a command tool for deploying functions. And the instructions for building faas-cli is available [here](https://github.com/JelixLi/Tetris/tree/main/openfaas/faas-cli#readme). And for simplicity, we also provided an already compiled version 
[here](https://github.com/JelixLi/Tetris/tree/main/openfaas/faas-cli#readme).

## Get gateway
gateway component is responsible for receiving and forward requests and accepting instructions from faas-cli. Tetris's gateway is an optimized version of the original version of openfaas. And for simplicity, we also provided an already compiled image.
```
$ sudo docker pull registry.cn-hangzhou.aliyuncs.com/gcr_cn/gateway:latest-dev

latest-dev: Pulling from gcr_cn/gateway
540db60ca938: Already exists 
4e83572ec7d7: Pull complete 
4f4fb700ef54: Pull complete 
440b2fce8e46: Pull complete 
3f1f27bce376: Pull complete 
722364792005: Pull complete 
5a470c7002ba: Pull complete 
Digest: sha256:ae07855b56eb2948702199e47dd2d5a39b77489ad9fba1e5563133fe7b9b5447
Status: Downloaded newer image for registry.cn-hangzhou.aliyuncs.com/gcr_cn/gateway:latest-dev
registry.cn-hangzhou.aliyuncs.com/gcr_cn/gateway:latest-dev

$ sudo docker tag registry.cn-hangzhou.aliyuncs.com/gcr_cn/gateway:latest-dev openfaas/gateway:latest-dev
```
## Get faas-netes
faas-netes component is responsible for autoscaling, scheduling and request load balancing. We also provided an already compiled image for quick usage.
```
$ sudo docker pull  registry.cn-hangzhou.aliyuncs.com/gcr_cn/faas-netes:latest-dev-bp

latest-dev-bp: Pulling from gcr_cn/faas-netes
79e9f2f55bf5: Pull complete 
c5300f1e502a: Pull complete 
94920840962d: Pull complete 
c531ae28f433: Pull complete 
Digest: sha256:e29879977eb0be6abf8406a4812ec07ba65f206b7396037c25d0b665a23b7584
Status: Downloaded newer image for registry.cn-hangzhou.aliyuncs.com/gcr_cn/faas-netes:latest-dev-bp
registry.cn-hangzhou.aliyuncs.com/gcr_cn/faas-netes:latest-dev-bp

$ sudo docker tag registry.cn-hangzhou.aliyuncs.com/gcr_cn/faas-netes:latest-dev-bp openfaas/faas-netes:latest-dev
```
## Create namespace
The namespace should be created first, and the namespace configuration is [here](https://github.com/JelixLi/Tetris/blob/main/openfaas/namespaces.yml). The openfaasdev is the namespace for system components and openfaasdev-fn is the namespace for functions.
```
$ sudo kubectl apply -f namespace.yaml

$ sudo kubectl get namespace | grep dev

openfaasdev            Active   114d
openfaasdev-fn         Active   114d
```
## Prepare system configurations
We provided a sample single node configuration file [here](https://github.com/JelixLi/Tetris/blob/main/openfaas/config/clusterCapConfig-dev-1.yml). 

*Note: This configuration file here assumes the node hostname to be kube-master.*

```
$ mkdir -p /home/tank/lijie/goWorkspace-dev/src/github.com/openfaas/faas-netes/yaml_1

$ mv clusterCapConfig-dev-1.yml /home/tank/lijie/goWorkspace-dev/src/github.com/openfaas/faas-netes/yaml_1/
```
Also additional configuration data need to be downloaded from [here](https://pan.baidu.com/s/1YuZzsIjUePm-3K2Jt44F6g?pwd=eqit).
```
$ mkdir -p /home/tank/lijie/goWorkspace-dev/src/github.com/openfaas/faas-netes/yaml_1/profiler/

$ tar -zxvf data.tar.gz -C /home/tank/lijie/goWorkspace-dev/src/github.com/openfaas/faas-netes/yaml_1/profiler/
```
## Deploy system components
The system components are [here](https://github.com/JelixLi/Tetris/tree/main/openfaas/components).

*Note: the item faas_loadgen_host in [gateway-dep.yaml](https://github.com/JelixLi/Tetris/blob/main/openfaas/components/gateway-dep.yml) should be modified to the ip address of your node before the deployment.*
```
$ sudo kubectl apply -f components/

$ sudo kubectl get deployment -n openfaasdev

NAME                          READY   UP-TO-DATE   AVAILABLE   AGE
basic-auth-plugindev          1/1     1            1           113d
cpuagentcontroller-deploy-0   1/1     1            1           20d
gatewaydev                    1/1     1            1           113d
prometheusdev                 1/1     1            1           113d
```
## Deploy functions
Before the deployment of functions, models should be downloaded from [here](https://1drv.ms/u/s!AoP-wwp0EkoEg04mK-wgWVarvb1j?e=TeFdiK&download=1).
```
$ mkdir -p /home/tank/lijie/serving_models/

$ tree /home/tank/lijie/serving_models/resnet-152-keras/

/home/tank/lijie/serving_models/resnet-152-keras/
└── 1
    ├── assets
    ├── saved_model.pb
    └── variables
        ├── variables.data-00000-of-00001
        └── variables.index

3 directories, 3 files
```
We have provided an example function [here](https://github.com/JelixLi/Tetris/blob/main/openfaas/resnet152.yaml).
```
$ faasdev-cli  deploy -f resnet152.yaml 

Deploying: resnet-152-keras.
WARNING! Communication is not secure, please consider using HTTPS. Letsencrypt.org offers free SSL/TLS certificates.

Deployed. 202 Accepted.
URL: http://127.0.0.1:31212/function/resnet-152-keras
```
Delete functions (if needed).
```
$ faasdev-cli remove  -f resnet152.yaml 

Deleting: resnet-152-keras.
Removing old function.
```
## Start load generator
We have pre-compiled the image of a load generator, and can be downloaded through following instructions.
```
$ sudo docker pull registry.cn-hangzhou.aliyuncs.com/gcr_cn/load_generator:single

single: Pulling from gcr_cn/load_generator
0e29546d541c: Pulling fs layer 
9b829c73b52b: Pulling fs layer 
cb5b7ae36172: Pulling fs layer 
6494e4811622: Waiting 
6f9f74896dfa: Pull complete 
fcb6d5f7c986: Pull complete 
7a72d131c196: Pull complete 
c4221d178521: Pull complete 
71d5c5b5a91f: Pull complete 
b8bb49f782af: Pull complete 
9a3b44001ade: Pull complete 
7d24f12c3225: Pull complete 
562eca055017: Pull complete 
552b82c68c4c: Pull complete 
4effcd1477d1: Pull complete 
e10b741b42b6: Pull complete 
4faa2785ff4d: Pull complete 
9330a0295911: Pull complete 
9117fbc354fc: Pull complete 
Digest: sha256:20e5fc2eb976b2af1bd87490bb3b38d1c373ea062bc5ae7c3fcb6afd688c26cd
Status: Downloaded newer image for registry.cn-hangzhou.aliyuncs.com/gcr_cn/load_generator:single
registry.cn-hangzhou.aliyuncs.com/gcr_cn/load_generator:single

$ sudo docker tag registry.cn-hangzhou.aliyuncs.com/gcr_cn/load_generator:single load_generator:single
```
Following instruction start a load generator, and there are three types of workloads (stable/period/burst). And the inference latencies could be found in the traces directories.
```
$ mkdir traces

$ sudo docker run --net=host -p 8081:8081 -v traces:/traces load_generator:single 192.168.1.136 resnet-152-keras stable 50 10 192.168.1.136

WARNING: Published ports are discarded when using host network mode
Gateway address: 192.168.1.136
Model: resnet-152-keras
Workload type: stable
Max qps: 50
Load period (minutes): 10
Load generator address :192.168.1.136
 * Serving Flask app 'load_generator' (lazy loading)
 * Environment: production
   WARNING: This is a development server. Do not use it in a production deployment.
   Use a production WSGI server instead.
 * Debug mode: off
 * Running on http://192.168.1.136:8081 (Press CTRL+C to quit)
```
## Memory measurement
For simplicity, we have provided simple [script](https://github.com/JelixLi/Tetris/tree/main/scripts) for measuring the total memory comsumed by inference function instances on a single node every 10s.
```
$ python total_memory.py

$ cat output.txt

2022-05-05 04:40:11.634803 62.07
2022-05-05 04:40:20.337647 62.09
2022-05-05 04:40:29.065235 62.09
2022-05-05 04:40:37.798275 62.09
2022-05-05 04:42:00.717909 62.15
2022-05-05 04:42:09.423884 62.15
2022-05-05 04:42:18.151213 62.15
2022-05-05 04:42:26.863815 62.15
2022-05-05 04:42:35.579574 62.15
2022-05-05 04:42:44.296924 62.15

$ python average_memory.py output.txt 

Average memory: 62.124
```
For comparison, we also provided an compiled image of INFless.
```
$ sudo docker pull registry.cn-hangzhou.aliyuncs.com/gcr_cn/faas-netes:latest-dev-bo
```
And then replace the openfaas/faas-netes:latest-dev with the newly downloaded image and restart the gateway component.
```
$ sudo docker tag registry.cn-hangzhou.aliyuncs.com/gcr_cn/faas-netes:latest-dev-bo openfaas/faas-netes:latest-dev

$ sudo kubectl get pods -n openfaasdev | grep gateway

gatewaydev-6bfb4cc9f8-x8jcx                    2/2     Running       0          148m

$ sudo kubectl delete pod gatewaydev-6bfb4cc9f8-x8jcx -n openfaasdev
```
Also, the old function should be deleted and replace it with the [new one](https://github.com/JelixLi/Tetris/blob/main/openfaas/resnet152-original.yaml).
