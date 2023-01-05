package controller

import (
	"fmt"
	"log"
	"strconv"
	"strings"
	"time"

	cpuRepository "github.com/openfaas/faas-netes/cpu/repository"
	"github.com/openfaas/faas-netes/gpu/repository"
	"github.com/openfaas/faas-netes/gpu/tools"
	gpuTypes "github.com/openfaas/faas-netes/gpu/types"
	apiv1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/client-go/kubernetes"
)

func createFuncInstance(funcName string, namespace string, funcPodConfig *gpuTypes.FuncPodConfig, podType string, clientset *kubernetes.Clientset) error {
	funcDeployStatus := repository.GetFunc(funcName)
	if funcDeployStatus == nil {
		return fmt.Errorf("scaler: function %s needed to be created instance does not exist in repository\n", funcName)
	}
	if podType == "i" {
		if funcDeployStatus.ExpectedReplicas == funcDeployStatus.AvailReplicas {
			repository.UpdateFuncExpectedReplicas(funcName, funcDeployStatus.ExpectedReplicas+1)
		} else {
			log.Printf("scaler: function %s create instance failed and try again\n", funcName)
			return fmt.Errorf("scaler: ExpectedReplicas!=AvailReplicas for function %s\n", funcName)
		}
	}

	nodeCap := repository.GetClusterCapConfig().ClusterCapacity[funcPodConfig.NodeGpuCpuAllocation.NodeTh]
	nodeLabelStrList := strings.Split(nodeCap.NodeLabel, "=")
	nodeSelector := map[string]string{} // init=map{}
	nodeSelector[nodeLabelStrList[0]] = nodeLabelStrList[1]
	funcDeployStatus.FuncSpec.Pod.Spec.NodeSelector = nodeSelector

	if len(funcDeployStatus.FuncSpec.Pod.Spec.Containers) == 0 {
		return fmt.Errorf("replicas: funcSpec.pod.spec.container's length is 0 error")
	}
	envItemSize := len(funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env)
	foundNum := 0
	for j := envItemSize - 1; j > 0 && foundNum < 8; j-- {
		if funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env[j].Name == "CUDA_VISIBLE_DEVICES" {
			funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env[j].Value = strconv.Itoa(nodeCap.GpuCapacity[funcPodConfig.NodeGpuCpuAllocation.CudaDeviceTh].CudaDeviceIndex)
			foundNum++
		} else if funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env[j].Name == "CUDA_MPS_ACTIVE_THREAD_PERCENTAGE" {
			funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env[j].Value = strconv.Itoa(int(funcPodConfig.GpuCorePercent))
			foundNum++
		} else if funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env[j].Name == "GPU_MEM_FRACTION" {
			funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env[j].Value = strconv.FormatFloat(funcPodConfig.GpuMemoryRate, 'f', 2, 32)
			foundNum++
		} else if funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env[j].Name == "BATCH_SIZE" {
			funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env[j].Value = strconv.Itoa(int(funcPodConfig.BatchSize))
			foundNum++
		} else if funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env[j].Name == "BATCH_TIMEOUT" {
			funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env[j].Value = strconv.Itoa(int(funcPodConfig.BatchTimeOut))
			foundNum++
		} else if funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env[j].Name == "MODEL_NAME" {
			funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env[j].Value = funcName
			foundNum++
		} else if funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env[j].Name == "ENABLE_BATCH" {
			if int(funcPodConfig.BatchSize) == 1 {
				funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env[j].Value = "false"
			} else {
				funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env[j].Value = "true"
			}
			foundNum++
		} else if funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env[j].Name == "BATCH_THREADS" {
			funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env[j].Value = strconv.Itoa(int(funcPodConfig.BatchThreads))
			foundNum++
		}
	}

	// 	# BATCH_SIZE
	// # BATCH_TIMEOUT
	// # ENABLE_BATCH
	// # BATCH_THREADS

	// funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env = append(funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env, apiv1.EnvVar{Name: "MODEL_NAME", Value: funcName})

	// if int(funcPodConfig.BatchSize) == 1 {
	// 	funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env = append(funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env, apiv1.EnvVar{Name: "ENABLE_BATCH", Value: "false"})
	// } else {
	// 	funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env = append(funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env, apiv1.EnvVar{Name: "ENABLE_BATCH", Value: "true"})
	// }

	funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].VolumeMounts = []apiv1.VolumeMount{
		apiv1.VolumeMount{Name: "serving-locks", MountPath: "/home/tank/lijie/serving_locks/"},
		// apiv1.VolumeMount{Name: "serving-memorys", MountPath: "/dev/shm/serving_memorys/"},
		apiv1.VolumeMount{Name: "serving-models", MountPath: "/models/" + funcName},
	}

	var host_path_type apiv1.HostPathType
	host_path_type = "Directory"

	funcDeployStatus.FuncSpec.Pod.Spec.Volumes = []apiv1.Volume{
		apiv1.Volume{Name: "serving-locks", VolumeSource: apiv1.VolumeSource{HostPath: &apiv1.HostPathVolumeSource{Path: "/home/tank/lijie/serving_locks/", Type: &host_path_type}}},
		// apiv1.Volume{Name: "serving-memorys", VolumeSource: apiv1.VolumeSource{HostPath: &apiv1.HostPathVolumeSource{Path: "/dev/shm/serving_memorys/", Type: &host_path_type}}},
		apiv1.Volume{Name: "serving-models", VolumeSource: apiv1.VolumeSource{HostPath: &apiv1.HostPathVolumeSource{Path: "/home/tank/lijie/serving_models/" + funcName, Type: &host_path_type}}},
	}

	funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].LivenessProbe = nil
	funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].ReadinessProbe = nil

	podName := funcName + "-n" + strconv.Itoa(funcPodConfig.NodeGpuCpuAllocation.NodeTh) +
		"-t" + strconv.Itoa(int(funcPodConfig.CpuThreads)) + "-g" + strconv.Itoa(nodeCap.GpuCapacity[funcPodConfig.NodeGpuCpuAllocation.CudaDeviceTh].CudaDeviceIndex) +
		"-s" + strconv.Itoa(int(funcPodConfig.GpuCorePercent)) + "-m" + strconv.FormatFloat(funcPodConfig.GpuMemoryRate*100, 'f', 0, 32) +
		"-b" + strconv.Itoa(int(funcPodConfig.BatchSize)) + "-qx" + strconv.Itoa(int(funcPodConfig.ReqPerSecondMax)) + "-qi" + strconv.Itoa(int(funcPodConfig.ReqPerSecondMin)) +
		"-" + tools.RandomText(10)

	funcDeployStatus.FuncSpec.Pod.Name = podName

	_, err := clientset.CoreV1().Pods(namespace).Create(funcDeployStatus.FuncSpec.Pod)
	if err != nil {
		log.Printf("scaler: error launching a new pod %s for function %s in namespace %s\n", podName, funcDeployStatus.FunctionName, namespace)
		log.Println(err.Error())
		return err
	}

	// log.Printf("replicas: scale function %s took: %fs \n", funcDeployStatus.FunctionName, time.Since(start).Seconds())

	go func() {
		cpuCoreThList := funcPodConfig.NodeGpuCpuAllocation.CpuCoreThList
		if cpuCoreThList != nil && len(cpuCoreThList) > 0 {
			cpuSocket := nodeCap.CpuCapacity[funcPodConfig.NodeGpuCpuAllocation.SocketTh].CpuStatus
			var build strings.Builder
			for _, coreTh := range cpuCoreThList {
				build.WriteString(strconv.Itoa(cpuSocket[coreTh].CpuCoreIndex)) //OS core index
				build.WriteString(",")
				build.WriteString(strconv.Itoa(cpuSocket[coreTh].CpuCoreIndex + nodeCap.HyperThreadOffset)) //OS core index hyper thread
				build.WriteString(",")
			}
			cpuCoreStr := build.String()
			build.Reset()

			coreBindErr := cpuRepository.AssignPodToCpuCoreSync(clientset, namespace, podName, funcPodConfig.NodeGpuCpuAllocation.NodeTh, cpuCoreStr[0:len(cpuCoreStr)-1])
			if coreBindErr != nil {
				log.Printf("scaler: bind cpu failed, podName=%s, nodeTh=%d, coreStr=%s\n",
					podName, funcPodConfig.NodeGpuCpuAllocation.NodeTh, cpuCoreStr[0:len(cpuCoreStr)-1])
			} else {
				log.Printf("scaler: bind cpu successfully, podName=%s, nodeTh=%d, coreStr=%s\n",
					podName, funcPodConfig.NodeGpuCpuAllocation.NodeTh, cpuCoreStr[0:len(cpuCoreStr)-1])
			}
		}
	}()

	// log.Printf("scaler: scaleup function %s 's Pod for differ %d successfully \n", funcDeployStatus.FunctionName, i+1)

	ip := ""
	ipGetTrys := 0
	for {
		// log.Printf("scaler: debug ip %s",ip)
		ipGetTrys++
		if ipGetTrys > 30 {
			log.Printf("scaler: create new pod %s failed since gets IP time out\n", podName)
			break
		}
		pods, podErr := clientset.CoreV1().Pods(namespace).Get(podName, metav1.GetOptions{})
		if podErr != nil {
			log.Println(podErr.Error())
		} else {
			if pods.Status.PodIP != "" {
				ip = pods.Status.PodIP
				log.Printf("scaler: create new pod %s successfully and go routine gets IP=%s\n",
					podName,
					pods.Status.PodIP)
				break
			}
		}
		time.Sleep(time.Second * 1)
	}

	if ip == "" {
		log.Printf("scaler: create new pod %s failed since go routine gets IP failed\n", podName)
		foregroundPolicy := metav1.DeletePropagationForeground
		opts := &metav1.DeleteOptions{PropagationPolicy: &foregroundPolicy}
		delError := clientset.CoreV1().Pods(namespace).Delete(podName, opts)
		if delError != nil {
			log.Printf("scaler: rollback to delete pod %s for function %s failed\n", podName, funcName)
		} else {
			log.Printf("scaler: rollback to delete pod %s for function %s successfully\n", podName, funcName)
		}
	} else {
		funcPodConfig.FuncPodName = podName
		funcPodConfig.FuncPodIp = ip
		funcPodConfig.FuncPodType = podType
		funcPodConfig.InactiveCounter = 0
		repository.AddFuncPodConfig(funcName, funcPodConfig)
		if podType == "i" {
			repository.UpdateFuncAvailReplicas(funcName, funcDeployStatus.AvailReplicas+1)
		}
	}

	if podType == "i" {
		if funcDeployStatus.AvailReplicas < funcDeployStatus.ExpectedReplicas {
			repository.UpdateFuncExpectedReplicas(funcName, funcDeployStatus.AvailReplicas)
		}

	}

	// ip := ""
	// if ip == "" {
	// 	funcPodConfig.FuncPodName = podName
	// 	funcPodConfig.FuncPodIp = ip
	// 	funcPodConfig.FuncPodType = podType
	// 	funcPodConfig.InactiveCounter = 0
	// 	repository.AddFuncPodConfig(funcName, funcPodConfig)
	// 	if podType == "i" {
	// 		repository.UpdateFuncAvailReplicas(funcName, funcDeployStatus.AvailReplicas + 1)
	// 	}
	// }

	return nil
}

func deleteFuncInstance(funcName string, namespace string, deletedFuncPodList []*gpuTypes.FuncPodConfig, clientset *kubernetes.Clientset) error {
	funcDeployStatus := repository.GetFunc(funcName)
	if funcDeployStatus == nil {
		return fmt.Errorf("scaler: function %s needed to be created instance does not exist\n", funcName)
	}

	foregroundPolicy := metav1.DeletePropagationForeground
	opts := &metav1.DeleteOptions{PropagationPolicy: &foregroundPolicy}
	for _, item := range deletedFuncPodList {
		repository.UpdateFuncExpectedReplicas(funcName, funcDeployStatus.ExpectedReplicas-1)
		//start := time.Now()
		err := clientset.CoreV1().Pods(namespace).Delete(item.FuncPodName, opts)
		if err != nil {
			log.Printf("scaler: function %s deleted pod %s failed \n", funcName, item.FuncPodName)
			repository.UpdateFuncExpectedReplicas(funcName, funcDeployStatus.ExpectedReplicas+1)
			return err
		}
		//log.Printf("scaler: function %s deleted pod %s successfully \n", funcName, item.FuncPodName)
		repository.DeleteFuncPodLocation(funcDeployStatus.FunctionName, item.FuncPodName)
		repository.UpdateFuncAvailReplicas(funcName, funcDeployStatus.AvailReplicas-1)
		//log.Printf("scaler: scale function %s took: %fs \n", funcDeployStatus.FunctionName, time.Since(start).Seconds())
	}

	// for _ , item := range deletedFuncPodList {
	// 	repository.UpdateFuncExpectedReplicas(funcName, funcDeployStatus.ExpectedReplicas-1)
	// 	//start := time.Now()

	// 	//log.Printf("scaler: function %s deleted pod %s successfully \n", funcName, item.FuncPodName)
	// 	repository.DeleteFuncPodLocation(funcDeployStatus.FunctionName, item.FuncPodName)
	// 	log.Printf("scaler: function %s deleted pod %s successfully \n", funcName, item.FuncPodName)
	// 	repository.UpdateFuncAvailReplicas(funcName, funcDeployStatus.AvailReplicas-1)
	// 	//log.Printf("scaler: scale function %s took: %fs \n", funcDeployStatus.FunctionName, time.Since(start).Seconds())
	// }
	return nil
}
