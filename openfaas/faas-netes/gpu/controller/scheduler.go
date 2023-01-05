package controller

import (
	"fmt"
	"log"
	"strconv"
	"sync"

	"github.com/openfaas/faas-netes/gpu/repository"
	gTypes "github.com/openfaas/faas-netes/gpu/types"
	"k8s.io/client-go/kubernetes"
)

var lock sync.Mutex

const CpuUsageRateThreshold = 0.8
const CpuFuncInstanceThreshold = 3

func CreatePreWarmPod(funcName string, namespace string, latencySLO float64, batchSize int32, clientset *kubernetes.Clientset) {
	log.Printf("scheduler:  CreatePreWarmPod %s ", funcName)
	funcStatus := repository.GetFunc(funcName)
	if funcStatus == nil {
		log.Printf("scheduler: warm function %s is nil in repository, error to read GPU memory", funcName)
		return
	}
	gpuMemAlloc, err := strconv.Atoi(funcStatus.FuncResources.GPU_Memory)
	if err == nil {
		//log.Printf("scheduler: warm reading GPU memory alloc of function %s = %d\n", funcName, gpuMemAlloc)
	} else {
		log.Println("scheduler: warm read memory error:", err.Error())
		return
	}

	resourcesConfigs, err := inferResourceConfigsWithBatch(funcName, latencySLO, batchSize, 1)
	if err != nil {
		log.Print(err.Error())
		wrappedErr := fmt.Errorf("scheduler: CreatePrewarmPod failed batch=%d cannot meet for function=%s, SLO=%f, reqArrivalRate=%d, residualReq=%d\n",
			batchSize, funcName, latencySLO, 1, 1)
		log.Println(wrappedErr)
		return
	} else {
		/*for _, item := range resourcesConfigs {
			log.Printf("scheduler: warm resourcesConfigs={funcName=%s, latencySLO=%f, expectTime=%d, batchSize=%d, cpuThreads=%d, gpuCorePercent=%d, maxCap=%d, minCap=%d}\n",
				funcName, latencySLO, item.ExecutionTime, batchSize, item.CpuThreads, item.GpuCorePercent, item.ReqPerSecondMax, item.ReqPerSecondMin)
		}*/
	}

	maxThroughputEfficiency := getMaxThroughputEfficiency(funcName)

	cpuConsumedThreadsPerSocket := int(0)
	cpuTotalThreadsPerSocket := int(0)
	cpuOverSell := 0
	gpuOverSell := 0
	gpuMemOverSellRate := float64(0)
	residualFindFlag := false

	gpuMemConsumedRate := float64(0)
	gpuCoreConsumedRate := float64(0)
	cpuConsumedRate := float64(0)
	slotCpuCapacity := float64(0)
	slotGpuCapacity := float64(0)
	slotUnitCapacity := float64(0)

	tempGpuCoreQuotaRate := float64(0)
	tempGpuMemQuotaRate := float64(0)
	tempCpuQuotaRate := float64(0)

	tempCRE := float64(0)
	maxCRE := float64(-1)
	maxCREConfigIndex := -1
	maxCRENodeIndex := -1
	maxCRESlotIndex := -1

	lock.Lock()
	defer lock.Unlock()
	//log.Println("scheduler: warm locked---------------------------")
	clusterCapConfig := repository.GetClusterCapConfig()

	for i := 0; i < len(clusterCapConfig.ClusterCapacity); i++ {
		cpuOverSell = clusterCapConfig.ClusterCapacity[i].CpuCoreOversell
		gpuOverSell = clusterCapConfig.ClusterCapacity[i].GpuCoreOversellPercentage
		gpuMemOverSellRate = clusterCapConfig.ClusterCapacity[i].GpuMemOversellRate

		cpuCapacity := clusterCapConfig.ClusterCapacity[i].CpuCapacity
		for j := 0; j < len(cpuCapacity) && residualFindFlag == false; j++ {

			cpuConsumedThreadsPerSocket = 0
			cpuTotalThreadsPerSocket = 0
			cpuStatus := cpuCapacity[j].CpuStatus
			for k := 0; k < len(cpuStatus); k++ {
				cpuConsumedThreadsPerSocket += cpuStatus[k].TotalFuncInstance
				cpuTotalThreadsPerSocket++
			}
			cpuConsumedThreadsPerSocket = cpuConsumedThreadsPerSocket << 1
			cpuTotalThreadsPerSocket = cpuTotalThreadsPerSocket << 1
			cpuConsumedRate = float64(cpuConsumedThreadsPerSocket) / float64(cpuTotalThreadsPerSocket+cpuOverSell)
			gpuCoreConsumedRate = clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuCoreUsageRate / (1.0 + float64(gpuOverSell)/100)
			gpuMemConsumedRate = clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuMemUsageRate / (1.0 + gpuMemOverSellRate)

			//log.Println()
			//			//log.Printf("scheduler: warm current node=%dth, socket=%dth, GPU=%dth, physical CpuConsumedRate=%f, GpuMemConsumedRate=%f, GpuCoreConsumedRate=%f",
			//			//	i,
			//			//	j,
			//			//	j+1,
			//			//	float64(cpuConsumedThreadsPerSocket) / float64(cpuTotalThreadsPerSocket),
			//			//	clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuMemUsageRate,
			//			//	clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuCoreUsageRate)

			slotCpuCapacity = float64(cpuTotalThreadsPerSocket+cpuOverSell) * 64
			slotGpuCapacity = float64(100+gpuOverSell) * 142
			slotUnitCapacity = slotCpuCapacity + slotGpuCapacity
			for k := 0; k < len(resourcesConfigs); k++ {
				if resourcesConfigs[k].GpuCorePercent == 0 {
					resourcesConfigs[k].GpuMemoryRate = 0
				} else {
					panic("Use GPU")
					resourcesConfigs[k].GpuMemoryRate = float64(gpuMemAlloc) / float64(clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuMemory)
				}

				tempCpuQuotaRate = float64(resourcesConfigs[k].CpuThreads) / float64(cpuTotalThreadsPerSocket+cpuOverSell)
				tempGpuCoreQuotaRate = float64(resourcesConfigs[k].GpuCorePercent) / float64(100+gpuOverSell)
				tempGpuMemQuotaRate = resourcesConfigs[k].GpuMemoryRate / (1.0 + gpuMemOverSellRate)

				targetCpuConsumedRate := cpuConsumedRate + tempCpuQuotaRate
				targetGpuConsumedRate := gpuCoreConsumedRate + tempGpuCoreQuotaRate
				targetGpuMemConsumedRate := gpuMemConsumedRate + tempGpuMemQuotaRate

				if targetCpuConsumedRate > 1.001 ||
					targetGpuConsumedRate > 1.001 ||
					targetGpuMemConsumedRate > 1.001 {
					//log.Printf("scheduler: warm current node has no enough resources for %dth pod config, skip to next pod config\n",k)
					continue
				} else {
					fragmentResource := slotCpuCapacity*(1.001-targetCpuConsumedRate) + slotGpuCapacity*(1.001-targetGpuConsumedRate)
					tempCRE = (float64(resourcesConfigs[k].ReqPerSecondMax) / maxThroughputEfficiency) / (fragmentResource / slotUnitCapacity)
					if tempCRE > maxCRE {
						maxCRE = tempCRE
						maxCRENodeIndex = i
						maxCRESlotIndex = j
						maxCREConfigIndex = k
					}
					//log.Printf("scheduler: warm current node has enough resources for %dth pod config, skip to next pod config\n",k)
				}
			}
		}
	}

	if maxCREConfigIndex == -1 || maxCRESlotIndex == -1 {
		log.Printf("scheduler: error! no pod resource config can be placed in the cluster\n")
	} else {
		var cpuCoreThList []int
		cpuStatus := clusterCapConfig.ClusterCapacity[maxCRENodeIndex].CpuCapacity[maxCRESlotIndex].CpuStatus
		neededCores := resourcesConfigs[maxCREConfigIndex].CpuThreads >> 1
		for k := 0; k < len(cpuStatus) && neededCores > 0; k++ {
			if cpuStatus[k].TotalFuncInstance == 0 {
				cpuCoreThList = append(cpuCoreThList, k)
				neededCores--
			}
		}
		for k := 0; k < len(cpuStatus) && neededCores > 0; k++ {
			if cpuStatus[k].TotalFuncInstance != 0 {
				if cpuStatus[k].TotalFuncInstance < CpuFuncInstanceThreshold && gTypes.LessEqual(cpuStatus[k].TotalCpuUsageRate, CpuUsageRateThreshold) {
					cpuCoreThList = append(cpuCoreThList, k)
					neededCores--
				}
			}
		}

		if neededCores > 0 {
			log.Printf("scheduler: error! warm failed to find enough CPU cores in current socket for residual neededCores=%d", neededCores)
		} else {
			//log.Printf("scheduler: warm decide to schedule pod on node=%dth, socket=%dth, GPU=%dth, physical cpuExpectConsumedThreads=%d (oversell=%d threads), gpuMemExpectConsumedRate=%f (oversell=%f), gpuCoreExpectConsumedRate=%f (oversell=%f)",
			//	i,
			//	j,
			//	cudaDeviceTh,
			//	cpuConsumedThreadsPerSocket + int(resourcesConfigs[pickConfigIndex].CpuThreads),
			//	cpuTotalThreadsPerSocket + cpuOverSell,
			//	gpuMemConsumedRate + resourcesConfigs[pickConfigIndex].GpuMemoryRate,
			//	1 + gpuMemOverSellRate,
			//	clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuCoreUsageRate + float64(resourcesConfigs[pickConfigIndex].GpuCorePercent) / 100,
			//	1 + float64(gpuOverSell)/100)

			cudaDeviceTh := maxCRESlotIndex + 1
			if resourcesConfigs[maxCREConfigIndex].GpuCorePercent == 0 {
				cudaDeviceTh = 0
			}

			resourcesConfigs[maxCREConfigIndex].NodeGpuCpuAllocation = &gTypes.NodeGpuCpuAllocation{
				NodeTh:        maxCRENodeIndex,
				SocketTh:      maxCRESlotIndex,
				CudaDeviceTh:  cudaDeviceTh,
				CpuCoreThList: cpuCoreThList,
			}

			createErr := createFuncInstance(funcName, namespace, resourcesConfigs[maxCREConfigIndex], "p", clientset)
			if createErr != nil {
				log.Println("scheduler: warm create prewarm function instance failed", createErr.Error())
			} else {
				//log.Printf("scheduler: warm create prewarm function instance for function %s successfully\n", funcName)
				residualFindFlag = true
			}
		}
	}
	//log.Println("scheduler: warm unlocked---------------------------")
	return
}

// type byValue []int32

// func (f byValue) Len() int {
// 	return len(f)
// }

// func (f byValue) Less(i, j int) bool {
// 	return f[i] < f[j]
// }

// func (f byValue) Swap(i, j int) {
// 	f[i], f[j] = f[j], f[i]
// }

// func ScaleUp(funcName string, namespace string, latencySLO float64, reqArrivalRate int32, supportBatchGroup []int32, clientset *kubernetes.Clientset) {
// 	supportBatchConcurrencyGroupSet, err_1 := getBatchAndConcurrencySet(funcName)
// 	if err_1 != nil {
// 		panic(err_1)
// 	}
// 	supportBatchGroupSetKeys := make([]int32, 0, len(supportBatchConcurrencyGroupSet))
// 	for key := range supportBatchConcurrencyGroupSet {
// 		supportBatchGroupSetKeys = append(supportBatchGroupSetKeys, key)
// 	}
// 	// sort.Ints(supportBatchGroupSetKeys)
// 	sort.Sort(byValue(supportBatchGroupSetKeys))

// 	//repository.UpdateFuncIsScalingIn(funcName,true)
// 	funcStatus := repository.GetFunc(funcName)
// 	if funcStatus == nil {
// 		log.Printf("scheduler: function %s is nil in repository, error to scale up", funcName)
// 		return
// 	}
// 	gpuMemAlloc, err := strconv.Atoi(funcStatus.FuncResources.GPU_Memory)
// 	if err == nil {
// 		//log.Printf("scheduler: reading GPU memory alloc of function %s = %d\n", funcName, gpuMemAlloc)
// 	} else {
// 		log.Println("scheduler: reading memory error:", err.Error())
// 		return
// 	}

// 	maxThroughputEfficiency := getMaxThroughputEfficiency(funcName)

// 	cpuConsumedThreadsPerSocket := 0
// 	cpuTotalThreadsPerSocket := 0
// 	cpuOverSell := 0
// 	gpuOverSell := 0
// 	gpuMemOverSellRate := float64(0)
// 	residualReq := reqArrivalRate
// 	residualFindFlag := false
// 	// batchTryNum := 0
// 	batchConcurrencyTryNum := 0

// 	gpuMemConsumedRate := float64(0)
// 	gpuCoreConsumedRate := float64(0)
// 	cpuConsumedRate := float64(0)
// 	slotCpuCapacity := float64(0)
// 	slotGpuCapacity := float64(0)
// 	slotUnitCapacity := float64(0)

// 	tempGpuCoreQuotaRate := float64(0)
// 	tempGpuMemQuotaRate := float64(0)
// 	tempCpuQuotaRate := float64(0)

// 	tempCRE := float64(0)
// 	maxCRE := float64(-1)
// 	maxCREConfigIndex := -1
// 	maxCRENodeIndex := -1
// 	maxCRESlotIndex := -1

// 	lock.Lock()
// 	defer lock.Unlock()
// 	for {
// 		if residualReq <= 0 {
// 			break
// 		}
// 		if residualReq > 0 {
// 			residualFindFlag = false
// 			if batchConcurrencyTryNum >= len(supportBatchConcurrencyGroupSet) {
// 				wrappedErr := fmt.Errorf("scheduler: failed to find suitable concurrency and batchsize for function=%s, SLO=%f, reqArrivalRate=%d, residualReq=%d\n",
// 					funcName, latencySLO, reqArrivalRate, residualReq)
// 				log.Println(wrappedErr)
// 				break
// 			}
// 			for batchConcurrencyIndex := len(supportBatchGroupSetKeys) - 1; batchConcurrencyIndex >= 0 && residualFindFlag == false; batchConcurrencyIndex-- {
// 				batch_concurrency_set := supportBatchConcurrencyGroupSet[supportBatchGroupSetKeys[batchConcurrencyIndex]]
// 				resourcesConfigs := []*gTypes.FuncPodConfig{}
// 				for _, bc := range batch_concurrency_set {
// 					rConfigs, errInfer := inferResourceConfigsWithBatchAndConcurrency(funcName, latencySLO, bc.Concurrency, bc.Batch, residualReq)
// 					if errInfer != nil {
// 						continue
// 					} else {
// 						resourcesConfigs = append(resourcesConfigs, rConfigs...)
// 					}
// 				}

// 				if len(resourcesConfigs) == 0 {
// 					batchConcurrencyTryNum++
// 					// log.Print(errInfer.Error())
// 					wrappedErr := fmt.Errorf("scheduler: batch concurrency=%d cannot meet for function=%s, SLO=%f, reqArrivalRate=%d, residualReq=%d\n",
// 						supportBatchGroupSetKeys[batchConcurrencyIndex], funcName, latencySLO, reqArrivalRate, residualReq)
// 					log.Println(wrappedErr)
// 					continue
// 				} else {
// 					/*for _ , item := range resourcesConfigs {
// 						log.Printf("scheduler: resourcesConfigs={funcName=%s, latencySLO=%f, expectTime=%d, batchSize=%d, cpuThreads=%d, gpuCorePercent=%d, maxCap=%d, minCap=%d}\n",
// 							funcName, latencySLO, item.ExecutionTime, supportBatchGroup[batchIndex], item.CpuThreads, item.GpuCorePercent, item.ReqPerSecondMax, item.ReqPerSecondMin)
// 					}*/
// 				}
// 				maxCRE = -1
// 				//log.Println("scheduler: warm locked---------------------------")
// 				clusterCapConfig := repository.GetClusterCapConfig()
// 				for i := 0; i < len(clusterCapConfig.ClusterCapacity); i++ {
// 					cpuOverSell = clusterCapConfig.ClusterCapacity[i].CpuCoreOversell
// 					gpuOverSell = clusterCapConfig.ClusterCapacity[i].GpuCoreOversellPercentage
// 					gpuMemOverSellRate = clusterCapConfig.ClusterCapacity[i].GpuMemOversellRate

// 					cpuCapacity := clusterCapConfig.ClusterCapacity[i].CpuCapacity
// 					for j := 0; j < len(cpuCapacity) && residualFindFlag == false; j++ {
// 						cpuConsumedThreadsPerSocket = 0
// 						cpuTotalThreadsPerSocket = 0
// 						cpuStatus := cpuCapacity[j].CpuStatus
// 						for k := 0; k < len(cpuStatus); k++ {
// 							cpuConsumedThreadsPerSocket += cpuStatus[k].TotalFuncInstance
// 							cpuTotalThreadsPerSocket++
// 						}
// 						cpuConsumedThreadsPerSocket = cpuConsumedThreadsPerSocket << 1
// 						cpuTotalThreadsPerSocket = cpuTotalThreadsPerSocket << 1
// 						cpuConsumedRate = float64(cpuConsumedThreadsPerSocket) / float64(cpuTotalThreadsPerSocket+cpuOverSell)
// 						gpuCoreConsumedRate = clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuCoreUsageRate / (1.0 + float64(gpuOverSell)/100)
// 						gpuMemConsumedRate = clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuMemUsageRate / (1.0 + gpuMemOverSellRate)

// 						//log.Println()
// 						//			//log.Printf("scheduler: warm current node=%dth, socket=%dth, GPU=%dth, physical CpuConsumedRate=%f, GpuMemConsumedRate=%f, GpuCoreConsumedRate=%f",
// 						//			//	i,
// 						//			//	j,
// 						//			//	j+1,
// 						//			//	float64(cpuConsumedThreadsPerSocket) / float64(cpuTotalThreadsPerSocket),
// 						//			//	clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuMemUsageRate,
// 						//			//	clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuCoreUsageRate)

// 						slotCpuCapacity = float64(cpuTotalThreadsPerSocket+cpuOverSell) * 64
// 						slotGpuCapacity = float64(100+gpuOverSell) * 142
// 						slotUnitCapacity = slotCpuCapacity + slotGpuCapacity
// 						for k := 0; k < len(resourcesConfigs); k++ {
// 							if resourcesConfigs[k].GpuCorePercent == 0 {
// 								resourcesConfigs[k].GpuMemoryRate = 0
// 							} else {
// 								panic("Use GPU")
// 								resourcesConfigs[k].GpuMemoryRate = float64(gpuMemAlloc) / float64(clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuMemory)
// 							}

// 							tempCpuQuotaRate = float64(resourcesConfigs[k].CpuThreads) / float64(cpuTotalThreadsPerSocket+cpuOverSell)
// 							tempGpuCoreQuotaRate = float64(resourcesConfigs[k].GpuCorePercent) / float64(100+gpuOverSell)
// 							tempGpuMemQuotaRate = resourcesConfigs[k].GpuMemoryRate / (1.0 + gpuMemOverSellRate)

// 							targetCpuConsumedRate := cpuConsumedRate + tempCpuQuotaRate
// 							targetGpuConsumedRate := gpuCoreConsumedRate + tempGpuCoreQuotaRate
// 							targetGpuMemConsumedRate := gpuMemConsumedRate + tempGpuMemQuotaRate

// 							if targetCpuConsumedRate > 1.001 ||
// 								targetGpuConsumedRate > 1.001 ||
// 								targetGpuMemConsumedRate > 1.001 {
// 								//log.Printf("scheduler: warm current node has no enough resources for %dth pod config, skip to next pod config\n",k)
// 								continue
// 							} else {
// 								fragmentResource := slotCpuCapacity*(1.001-targetCpuConsumedRate) + slotGpuCapacity*(1.001-targetGpuConsumedRate)
// 								tempCRE = (float64(resourcesConfigs[k].ReqPerSecondMax) / maxThroughputEfficiency) / (fragmentResource / slotUnitCapacity)
// 								if tempCRE > maxCRE {
// 									maxCRE = tempCRE
// 									maxCRENodeIndex = i
// 									maxCRESlotIndex = j
// 									maxCREConfigIndex = k
// 								}
// 								//log.Printf("scheduler: warm current node has enough resources for %dth pod config, skip to next pod config\n",k)
// 							}
// 						}
// 					}
// 				}

// 				if maxCREConfigIndex == -1 || maxCRESlotIndex == -1 {
// 					log.Printf("scheduler: error! no pod resource config can be placed in the cluster\n")
// 				} else {
// 					var cpuCoreThList []int
// 					cpuStatus := clusterCapConfig.ClusterCapacity[maxCRENodeIndex].CpuCapacity[maxCRESlotIndex].CpuStatus
// 					neededCores := resourcesConfigs[maxCREConfigIndex].CpuThreads >> 1
// 					for k := 0; k < len(cpuStatus) && neededCores > 0; k++ {
// 						if cpuStatus[k].TotalFuncInstance == 0 {
// 							cpuCoreThList = append(cpuCoreThList, k)
// 							neededCores--
// 						}
// 					}
// 					for k := 0; k < len(cpuStatus) && neededCores > 0; k++ {
// 						if cpuStatus[k].TotalFuncInstance != 0 {
// 							if cpuStatus[k].TotalFuncInstance < CpuFuncInstanceThreshold && gTypes.LessEqual(cpuStatus[k].TotalCpuUsageRate, CpuUsageRateThreshold) {
// 								cpuCoreThList = append(cpuCoreThList, k)
// 								neededCores--
// 							}
// 						}
// 					}

// 					if neededCores > 0 {
// 						log.Printf("scheduler: error! warm failed to find enough CPU cores in current socket for residual neededCores=%d", neededCores)
// 					} else {
// 						//log.Printf("scheduler: warm decide to schedule pod on node=%dth, socket=%dth, GPU=%dth, physical cpuExpectConsumedThreads=%d (oversell=%d threads), gpuMemExpectConsumedRate=%f (oversell=%f), gpuCoreExpectConsumedRate=%f (oversell=%f)",
// 						//	i,
// 						//	j,
// 						//	cudaDeviceTh,
// 						//	cpuConsumedThreadsPerSocket + int(resourcesConfigs[pickConfigIndex].CpuThreads),
// 						//	cpuTotalThreadsPerSocket + cpuOverSell,
// 						//	gpuMemConsumedRate + resourcesConfigs[pickConfigIndex].GpuMemoryRate,
// 						//	1 + gpuMemOverSellRate,
// 						//	clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuCoreUsageRate + float64(resourcesConfigs[pickConfigIndex].GpuCorePercent) / 100,
// 						//	1 + float64(gpuOverSell)/100)

// 						cudaDeviceTh := maxCRESlotIndex + 1
// 						if resourcesConfigs[maxCREConfigIndex].GpuCorePercent == 0 { //if only CPU are allocated
// 							cudaDeviceTh = 0
// 						}

// 						resourcesConfigs[maxCREConfigIndex].NodeGpuCpuAllocation = &gTypes.NodeGpuCpuAllocation{
// 							NodeTh:        maxCRENodeIndex,
// 							SocketTh:      maxCRESlotIndex,
// 							CudaDeviceTh:  cudaDeviceTh,
// 							CpuCoreThList: cpuCoreThList,
// 						}

// 						createErr := createFuncInstance(funcName, namespace, resourcesConfigs[maxCREConfigIndex], "i", clientset)
// 						if createErr != nil {
// 							log.Println("scheduler: create function instance failed ", createErr.Error())
// 						} else {
// 							//log.Printf("scheduler: create function instance for function%s successfully, residualReq=%d-%d=%d \n",
// 							//	funcName, residualReq, resourcesConfigs[pickConfigIndex].ReqPerSecondMax, residualReq - resourcesConfigs[pickConfigIndex].ReqPerSecondMax)
// 							residualReq = residualReq - resourcesConfigs[maxCREConfigIndex].ReqPerSecondMax
// 						}
// 						residualFindFlag = true
// 						batchConcurrencyTryNum = 0
// 					}
// 				}
// 			}
// 		}
// 	}
// 	return
// }

func CB_ScaleUp(funcName string, namespace string, latencySLO float64, reqArrivalRate int32, supportBatchGroup []int32, clientset *kubernetes.Clientset) {
	// supportBatchConcurrencyGroupSet, err_1 := getBatchAndConcurrencySet(funcName)
	// if err_1 != nil {
	// 	panic(err_1)
	// }
	// supportBatchGroupSetKeys := make([]int32, 0, len(supportBatchConcurrencyGroupSet))
	// for key := range supportBatchConcurrencyGroupSet {
	// 	supportBatchGroupSetKeys = append(supportBatchGroupSetKeys, key)
	// }
	// // sort.Ints(supportBatchGroupSetKeys)
	// sort.Sort(byValue(supportBatchGroupSetKeys))

	//repository.UpdateFuncIsScalingIn(funcName,true)
	log.Println("debug-666")
	funcStatus := repository.GetFunc(funcName)
	if funcStatus == nil {
		log.Printf("scheduler: function %s is nil in repository, error to scale up", funcName)
		return
	}
	gpuMemAlloc, err := strconv.Atoi(funcStatus.FuncResources.GPU_Memory)
	if err == nil {
		//log.Printf("scheduler: reading GPU memory alloc of function %s = %d\n", funcName, gpuMemAlloc)
	} else {
		log.Println("scheduler: reading memory error:", err.Error())
		return
	}

	maxThroughputEfficiency := getMaxThroughputEfficiency(funcName)

	cpuConsumedThreadsPerSocket := 0
	cpuTotalThreadsPerSocket := 0
	cpuOverSell := 0
	gpuOverSell := 0
	gpuMemOverSellRate := float64(0)
	residualReq := reqArrivalRate
	// residualFindFlag := false
	// batchTryNum := 0
	// batchConcurrencyTryNum := 0

	gpuMemConsumedRate := float64(0)
	gpuCoreConsumedRate := float64(0)
	cpuConsumedRate := float64(0)
	slotCpuCapacity := float64(0)
	slotGpuCapacity := float64(0)
	slotUnitCapacity := float64(0)

	tempGpuCoreQuotaRate := float64(0)
	tempGpuMemQuotaRate := float64(0)
	tempCpuQuotaRate := float64(0)

	tempCRE := float64(0)
	maxCRE := float64(-1)
	maxCREConfigIndex := -1
	maxCRENodeIndex := -1
	maxCRESlotIndex := -1
	maxShare := float64(-1)
	tempShare := float64(0)

	funcShareMem := GetFuncShareMem()

	bcs, cerr := getConfigs(funcName)
	if cerr != nil {
		log.Println(cerr)
		return
	}
	IC_Sort(&bcs)

	lock.Lock()
	defer lock.Unlock()

	for {
		if residualReq <= 0 {
			break
		}
		if residualReq > 0 {
			// residualFindFlag = false

			// bcs, cerr := GetConfigs(funcName)
			// if cerr != nil {
			// 	log.Println(cerr)
			// 	break
			// }

			resourcesConfigs, errInfer := inferResourceConfigsWithBatchAndConcurrencyAll(funcName, &bcs, latencySLO, residualReq)
			if errInfer != nil {
				log.Println(errInfer)
				break
			}

			maxCRE = -1
			maxShare = -1

			clusterCapConfig := repository.GetClusterCapConfig()
			for i := 0; i < len(clusterCapConfig.ClusterCapacity); i++ {
				cpuOverSell = clusterCapConfig.ClusterCapacity[i].CpuCoreOversell
				gpuOverSell = clusterCapConfig.ClusterCapacity[i].GpuCoreOversellPercentage
				gpuMemOverSellRate = clusterCapConfig.ClusterCapacity[i].GpuMemOversellRate

				tempShare = repository.GetMachineShareMem(int32(i), funcName, funcShareMem)

				cpuCapacity := clusterCapConfig.ClusterCapacity[i].CpuCapacity
				for j := 0; j < len(cpuCapacity); j++ {
					cpuConsumedThreadsPerSocket = 0
					cpuTotalThreadsPerSocket = 0
					cpuStatus := cpuCapacity[j].CpuStatus
					for k := 0; k < len(cpuStatus); k++ {
						cpuConsumedThreadsPerSocket += cpuStatus[k].TotalFuncInstance
						cpuTotalThreadsPerSocket++
					}
					cpuConsumedThreadsPerSocket = cpuConsumedThreadsPerSocket << 1
					cpuTotalThreadsPerSocket = cpuTotalThreadsPerSocket << 1
					cpuConsumedRate = float64(cpuConsumedThreadsPerSocket) / float64(cpuTotalThreadsPerSocket+cpuOverSell)
					gpuCoreConsumedRate = clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuCoreUsageRate / (1.0 + float64(gpuOverSell)/100)
					gpuMemConsumedRate = clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuMemUsageRate / (1.0 + gpuMemOverSellRate)

					//log.Println()
					//			//log.Printf("scheduler: warm current node=%dth, socket=%dth, GPU=%dth, physical CpuConsumedRate=%f, GpuMemConsumedRate=%f, GpuCoreConsumedRate=%f",
					//			//	i,
					//			//	j,
					//			//	j+1,
					//			//	float64(cpuConsumedThreadsPerSocket) / float64(cpuTotalThreadsPerSocket),
					//			//	clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuMemUsageRate,
					//			//	clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuCoreUsageRate)

					slotCpuCapacity = float64(cpuTotalThreadsPerSocket+cpuOverSell) * 64
					slotGpuCapacity = float64(100+gpuOverSell) * 142
					slotUnitCapacity = slotCpuCapacity + slotGpuCapacity
					for k := 0; k < len(resourcesConfigs); k++ {
						if resourcesConfigs[k].GpuCorePercent == 0 {
							resourcesConfigs[k].GpuMemoryRate = 0
						} else {
							panic("Use GPU")
							resourcesConfigs[k].GpuMemoryRate = float64(gpuMemAlloc) / float64(clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuMemory)
						}

						tempCpuQuotaRate = float64(resourcesConfigs[k].CpuThreads) / float64(cpuTotalThreadsPerSocket+cpuOverSell)
						tempGpuCoreQuotaRate = float64(resourcesConfigs[k].GpuCorePercent) / float64(100+gpuOverSell)
						tempGpuMemQuotaRate = resourcesConfigs[k].GpuMemoryRate / (1.0 + gpuMemOverSellRate)

						targetCpuConsumedRate := cpuConsumedRate + tempCpuQuotaRate
						targetGpuConsumedRate := gpuCoreConsumedRate + tempGpuCoreQuotaRate
						targetGpuMemConsumedRate := gpuMemConsumedRate + tempGpuMemQuotaRate

						if targetCpuConsumedRate > 1.001 ||
							targetGpuConsumedRate > 1.001 ||
							targetGpuMemConsumedRate > 1.001 {
							//log.Printf("scheduler: warm current node has no enough resources for %dth pod config, skip to next pod config\n",k)
							continue
						} else {
							fragmentResource := slotCpuCapacity*(1.001-targetCpuConsumedRate) + slotGpuCapacity*(1.001-targetGpuConsumedRate)
							tempCRE = (float64(resourcesConfigs[k].ReqPerSecondMax) / maxThroughputEfficiency) / (fragmentResource / slotUnitCapacity)
							// tempShare = tempShare * a + tempCRE * (1 - a)
							if tempShare > maxShare {
								maxShare = tempShare
								maxCRENodeIndex = i
								maxCRESlotIndex = j
								maxCREConfigIndex = k
								maxCRE = tempCRE
							} else if (int64(tempShare*100) == int64(maxShare*100)) && (tempCRE > maxCRE) {
								// // (int64(math.Abs(tempShare-maxShare)) < 10)
								// maxShare = tempShare
								// maxCRENodeIndex = i
								// maxCRESlotIndex = j
								// maxCREConfigIndex = k
								// maxCRE = tempCRE
							}
							//log.Printf("scheduler: warm current node has enough resources for %dth pod config, skip to next pod config\n",k)
						}
					}
				}
			}

			if maxCREConfigIndex == -1 || maxCRESlotIndex == -1 {
				log.Printf("scheduler: error! no pod resource config can be placed in the cluster\n")
			} else {
				var cpuCoreThList []int
				cpuStatus := clusterCapConfig.ClusterCapacity[maxCRENodeIndex].CpuCapacity[maxCRESlotIndex].CpuStatus
				neededCores := resourcesConfigs[maxCREConfigIndex].CpuThreads >> 1
				for k := 0; k < len(cpuStatus) && neededCores > 0; k++ {
					if cpuStatus[k].TotalFuncInstance == 0 {
						cpuCoreThList = append(cpuCoreThList, k)
						neededCores--
					}
				}
				for k := 0; k < len(cpuStatus) && neededCores > 0; k++ {
					if cpuStatus[k].TotalFuncInstance != 0 {
						if cpuStatus[k].TotalFuncInstance < CpuFuncInstanceThreshold && gTypes.LessEqual(cpuStatus[k].TotalCpuUsageRate, CpuUsageRateThreshold) {
							cpuCoreThList = append(cpuCoreThList, k)
							neededCores--
						}
					}
				}

				if neededCores > 0 {
					log.Printf("scheduler: error! warm failed to find enough CPU cores in current socket for residual neededCores=%d", neededCores)
				} else {
					//log.Printf("scheduler: warm decide to schedule pod on node=%dth, socket=%dth, GPU=%dth, physical cpuExpectConsumedThreads=%d (oversell=%d threads), gpuMemExpectConsumedRate=%f (oversell=%f), gpuCoreExpectConsumedRate=%f (oversell=%f)",
					//	i,
					//	j,
					//	cudaDeviceTh,
					//	cpuConsumedThreadsPerSocket + int(resourcesConfigs[pickConfigIndex].CpuThreads),
					//	cpuTotalThreadsPerSocket + cpuOverSell,
					//	gpuMemConsumedRate + resourcesConfigs[pickConfigIndex].GpuMemoryRate,
					//	1 + gpuMemOverSellRate,
					//	clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuCoreUsageRate + float64(resourcesConfigs[pickConfigIndex].GpuCorePercent) / 100,
					//	1 + float64(gpuOverSell)/100)

					cudaDeviceTh := maxCRESlotIndex + 1
					if resourcesConfigs[maxCREConfigIndex].GpuCorePercent == 0 { //if only CPU are allocated
						cudaDeviceTh = 0
					}

					resourcesConfigs[maxCREConfigIndex].NodeGpuCpuAllocation = &gTypes.NodeGpuCpuAllocation{
						NodeTh:        maxCRENodeIndex,
						SocketTh:      maxCRESlotIndex,
						CudaDeviceTh:  cudaDeviceTh,
						CpuCoreThList: cpuCoreThList,
					}

					createErr := createFuncInstance(funcName, namespace, resourcesConfigs[maxCREConfigIndex], "i", clientset)
					if createErr != nil {
						log.Println("scheduler: create function instance failed ", createErr.Error())
					} else {
						//log.Printf("scheduler: create function instance for function%s successfully, residualReq=%d-%d=%d \n",
						//	funcName, residualReq, resourcesConfigs[pickConfigIndex].ReqPerSecondMax, residualReq - resourcesConfigs[pickConfigIndex].ReqPerSecondMax)
						residualReq = residualReq - resourcesConfigs[maxCREConfigIndex].ReqPerSecondMax
					}
					// residualFindFlag = true
					// batchConcurrencyTryNum = 0
				}
			}
		}
	}
	return
}

// func ScaleUp(funcName string, namespace string, latencySLO float64, reqArrivalRate int32, supportBatchGroup []int32, clientset *kubernetes.Clientset) {
// 	supportBatchGroup, err_1 := getBatchSet(funcName)
// 	if err_1 != nil {
// 		panic(err_1)
// 	}
// 	//repository.UpdateFuncIsScalingIn(funcName,true)
// 	funcStatus := repository.GetFunc(funcName)
// 	if funcStatus == nil {
// 		log.Printf("scheduler: function %s is nil in repository, error to scale up", funcName)
// 		return
// 	}
// 	gpuMemAlloc, err := strconv.Atoi(funcStatus.FuncResources.GPU_Memory)
// 	if err == nil {
// 		//log.Printf("scheduler: reading GPU memory alloc of function %s = %d\n", funcName, gpuMemAlloc)
// 	} else {
// 		log.Println("scheduler: reading memory error:", err.Error())
// 		return
// 	}

// 	maxThroughputEfficiency := getMaxThroughputEfficiency(funcName)

// 	cpuConsumedThreadsPerSocket := 0
// 	cpuTotalThreadsPerSocket := 0
// 	cpuOverSell := 0
// 	gpuOverSell := 0
// 	gpuMemOverSellRate := float64(0)
// 	residualReq := reqArrivalRate
// 	residualFindFlag := false
// 	batchTryNum := 0

// 	gpuMemConsumedRate := float64(0)
// 	gpuCoreConsumedRate := float64(0)
// 	cpuConsumedRate := float64(0)
// 	slotCpuCapacity := float64(0)
// 	slotGpuCapacity := float64(0)
// 	slotUnitCapacity := float64(0)

// 	tempGpuCoreQuotaRate := float64(0)
// 	tempGpuMemQuotaRate := float64(0)
// 	tempCpuQuotaRate := float64(0)

// 	tempCRE := float64(0)
// 	maxCRE := float64(-1)
// 	maxCREConfigIndex := -1
// 	maxCRENodeIndex := -1
// 	maxCRESlotIndex := -1

// 	lock.Lock()
// 	defer lock.Unlock()
// 	for {
// 		if residualReq <= 0 {
// 			break
// 		}
// 		if residualReq > 0 {
// 			residualFindFlag = false
// 			if batchTryNum >= len(supportBatchGroup) {
// 				wrappedErr := fmt.Errorf("scheduler: failed to find suitable batchsize for function=%s, SLO=%f, reqArrivalRate=%d, residualReq=%d\n",
// 					funcName, latencySLO, reqArrivalRate, residualReq)
// 				log.Println(wrappedErr)
// 				break
// 			}
// 			for batchIndex := 0; batchIndex < len(supportBatchGroup) && residualFindFlag == false; batchIndex++ {
// 				resourcesConfigs, errInfer := inferResourceConfigsWithBatch(funcName, latencySLO, supportBatchGroup[batchIndex], residualReq)
// 				if errInfer != nil {
// 					batchTryNum++
// 					log.Print(errInfer.Error())
// 					wrappedErr := fmt.Errorf("scheduler: batch=%d cannot meet for function=%s, SLO=%f, reqArrivalRate=%d, residualReq=%d\n",
// 						supportBatchGroup[batchIndex], funcName, latencySLO, reqArrivalRate, residualReq)
// 					log.Println(wrappedErr)
// 					continue
// 				} else {
// 					/*for _ , item := range resourcesConfigs {
// 						log.Printf("scheduler: resourcesConfigs={funcName=%s, latencySLO=%f, expectTime=%d, batchSize=%d, cpuThreads=%d, gpuCorePercent=%d, maxCap=%d, minCap=%d}\n",
// 							funcName, latencySLO, item.ExecutionTime, supportBatchGroup[batchIndex], item.CpuThreads, item.GpuCorePercent, item.ReqPerSecondMax, item.ReqPerSecondMin)
// 					}*/
// 				}
// 				maxCRE = -1

// 				//log.Println("scheduler: warm locked---------------------------")
// 				clusterCapConfig := repository.GetClusterCapConfig()
// 				for i := 0; i < len(clusterCapConfig.ClusterCapacity); i++ {
// 					cpuOverSell = clusterCapConfig.ClusterCapacity[i].CpuCoreOversell
// 					gpuOverSell = clusterCapConfig.ClusterCapacity[i].GpuCoreOversellPercentage
// 					gpuMemOverSellRate = clusterCapConfig.ClusterCapacity[i].GpuMemOversellRate

// 					cpuCapacity := clusterCapConfig.ClusterCapacity[i].CpuCapacity
// 					for j := 0; j < len(cpuCapacity) && residualFindFlag == false; j++ {
// 						cpuConsumedThreadsPerSocket = 0
// 						cpuTotalThreadsPerSocket = 0
// 						cpuStatus := cpuCapacity[j].CpuStatus
// 						for k := 0; k < len(cpuStatus); k++ {
// 							cpuConsumedThreadsPerSocket += cpuStatus[k].TotalFuncInstance
// 							cpuTotalThreadsPerSocket++
// 						}
// 						cpuConsumedThreadsPerSocket = cpuConsumedThreadsPerSocket << 1
// 						cpuTotalThreadsPerSocket = cpuTotalThreadsPerSocket << 1
// 						cpuConsumedRate = float64(cpuConsumedThreadsPerSocket) / float64(cpuTotalThreadsPerSocket+cpuOverSell)
// 						gpuCoreConsumedRate = clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuCoreUsageRate / (1.0 + float64(gpuOverSell)/100)
// 						gpuMemConsumedRate = clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuMemUsageRate / (1.0 + gpuMemOverSellRate)

// 						//log.Println()
// 						//			//log.Printf("scheduler: warm current node=%dth, socket=%dth, GPU=%dth, physical CpuConsumedRate=%f, GpuMemConsumedRate=%f, GpuCoreConsumedRate=%f",
// 						//			//	i,
// 						//			//	j,
// 						//			//	j+1,
// 						//			//	float64(cpuConsumedThreadsPerSocket) / float64(cpuTotalThreadsPerSocket),
// 						//			//	clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuMemUsageRate,
// 						//			//	clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuCoreUsageRate)

// 						slotCpuCapacity = float64(cpuTotalThreadsPerSocket+cpuOverSell) * 64
// 						slotGpuCapacity = float64(100+gpuOverSell) * 142
// 						slotUnitCapacity = slotCpuCapacity + slotGpuCapacity
// 						for k := 0; k < len(resourcesConfigs); k++ {
// 							if resourcesConfigs[k].GpuCorePercent == 0 {
// 								resourcesConfigs[k].GpuMemoryRate = 0
// 							} else {
// 								panic("Use GPU")
// 								resourcesConfigs[k].GpuMemoryRate = float64(gpuMemAlloc) / float64(clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuMemory)
// 							}

// 							tempCpuQuotaRate = float64(resourcesConfigs[k].CpuThreads) / float64(cpuTotalThreadsPerSocket+cpuOverSell)
// 							tempGpuCoreQuotaRate = float64(resourcesConfigs[k].GpuCorePercent) / float64(100+gpuOverSell)
// 							tempGpuMemQuotaRate = resourcesConfigs[k].GpuMemoryRate / (1.0 + gpuMemOverSellRate)

// 							targetCpuConsumedRate := cpuConsumedRate + tempCpuQuotaRate
// 							targetGpuConsumedRate := gpuCoreConsumedRate + tempGpuCoreQuotaRate
// 							targetGpuMemConsumedRate := gpuMemConsumedRate + tempGpuMemQuotaRate

// 							if targetCpuConsumedRate > 1.001 ||
// 								targetGpuConsumedRate > 1.001 ||
// 								targetGpuMemConsumedRate > 1.001 {
// 								//log.Printf("scheduler: warm current node has no enough resources for %dth pod config, skip to next pod config\n",k)
// 								continue
// 							} else {
// 								fragmentResource := slotCpuCapacity*(1.001-targetCpuConsumedRate) + slotGpuCapacity*(1.001-targetGpuConsumedRate)
// 								tempCRE = (float64(resourcesConfigs[k].ReqPerSecondMax) / maxThroughputEfficiency) / (fragmentResource / slotUnitCapacity)
// 								if tempCRE > maxCRE {
// 									maxCRE = tempCRE
// 									maxCRENodeIndex = i
// 									maxCRESlotIndex = j
// 									maxCREConfigIndex = k
// 								}
// 								//log.Printf("scheduler: warm current node has enough resources for %dth pod config, skip to next pod config\n",k)
// 							}
// 						}
// 					}
// 				}

// 				if maxCREConfigIndex == -1 || maxCRESlotIndex == -1 {
// 					log.Printf("scheduler: error! no pod resource config can be placed in the cluster\n")
// 				} else {
// 					var cpuCoreThList []int
// 					cpuStatus := clusterCapConfig.ClusterCapacity[maxCRENodeIndex].CpuCapacity[maxCRESlotIndex].CpuStatus
// 					neededCores := resourcesConfigs[maxCREConfigIndex].CpuThreads >> 1
// 					for k := 0; k < len(cpuStatus) && neededCores > 0; k++ {
// 						if cpuStatus[k].TotalFuncInstance == 0 {
// 							cpuCoreThList = append(cpuCoreThList, k)
// 							neededCores--
// 						}
// 					}
// 					for k := 0; k < len(cpuStatus) && neededCores > 0; k++ {
// 						if cpuStatus[k].TotalFuncInstance != 0 {
// 							if cpuStatus[k].TotalFuncInstance < CpuFuncInstanceThreshold && gTypes.LessEqual(cpuStatus[k].TotalCpuUsageRate, CpuUsageRateThreshold) {
// 								cpuCoreThList = append(cpuCoreThList, k)
// 								neededCores--
// 							}
// 						}
// 					}

// 					if neededCores > 0 {
// 						log.Printf("scheduler: error! warm failed to find enough CPU cores in current socket for residual neededCores=%d", neededCores)
// 					} else {
// 						//log.Printf("scheduler: warm decide to schedule pod on node=%dth, socket=%dth, GPU=%dth, physical cpuExpectConsumedThreads=%d (oversell=%d threads), gpuMemExpectConsumedRate=%f (oversell=%f), gpuCoreExpectConsumedRate=%f (oversell=%f)",
// 						//	i,
// 						//	j,
// 						//	cudaDeviceTh,
// 						//	cpuConsumedThreadsPerSocket + int(resourcesConfigs[pickConfigIndex].CpuThreads),
// 						//	cpuTotalThreadsPerSocket + cpuOverSell,
// 						//	gpuMemConsumedRate + resourcesConfigs[pickConfigIndex].GpuMemoryRate,
// 						//	1 + gpuMemOverSellRate,
// 						//	clusterCapConfig.ClusterCapacity[i].GpuCapacity[j+1].TotalGpuCoreUsageRate + float64(resourcesConfigs[pickConfigIndex].GpuCorePercent) / 100,
// 						//	1 + float64(gpuOverSell)/100)

// 						cudaDeviceTh := maxCRESlotIndex + 1
// 						if resourcesConfigs[maxCREConfigIndex].GpuCorePercent == 0 {
// 							cudaDeviceTh = 0
// 						}

// 						resourcesConfigs[maxCREConfigIndex].NodeGpuCpuAllocation = &gTypes.NodeGpuCpuAllocation{
// 							NodeTh:        maxCRENodeIndex,
// 							SocketTh:      maxCRESlotIndex,
// 							CudaDeviceTh:  cudaDeviceTh,
// 							CpuCoreThList: cpuCoreThList,
// 						}

// 						createErr := createFuncInstance(funcName, namespace, resourcesConfigs[maxCREConfigIndex], "i", clientset)
// 						if createErr != nil {
// 							log.Println("scheduler: create function instance failed ", createErr.Error())
// 						} else {
// 							log.Printf("scheduler: create function instance for function%s successfully, residualReq=%d-%d=%d \n",
// 								funcName, residualReq, resourcesConfigs[maxCREConfigIndex].ReqPerSecondMax, residualReq-resourcesConfigs[maxCREConfigIndex].ReqPerSecondMax)
// 							residualReq = residualReq - resourcesConfigs[maxCREConfigIndex].ReqPerSecondMax
// 						}
// 						residualFindFlag = true
// 						batchTryNum = 0
// 					}
// 				}
// 			}
// 		}
// 	}
// 	return
// }

func ScaleUpFuncCapacity(funcName string, namespace string, latencySLO float64, reqArrivalRate int32, supportBatchGroup []int32, clientset *kubernetes.Clientset) {
	CB_ScaleUp(funcName, namespace, latencySLO, reqArrivalRate, supportBatchGroup, clientset)
}

func ScaleDownFuncCapacity(funcName string, namespace string, deletedFuncPodConfig []*gTypes.FuncPodConfig, clientset *kubernetes.Clientset) {
	lock.Lock()
	//log.Println("scheduler: scale down locked---------------------------")
	err := deleteFuncInstance(funcName, namespace, deletedFuncPodConfig, clientset)
	if err != nil {
		log.Println("scheduler: delete function instance failed ", err.Error())
	} else {
		//log.Println("scheduler: delete function instance successfully")
	}
	//log.Println("scheduler: scale down unlocked---------------------------")
	lock.Unlock()
	return
}
