package controller

import (
	"errors"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"sort"
	"strconv"
	"strings"

	gTypes "github.com/openfaas/faas-netes/gpu/types"
)

var maxThroughputEfficiencyMap map[string]float64

type CB struct {
	Concurrency int32
	Batch       int32
}

type IConfigs struct {
	Concurrency int32
	Batch       int32
	CPUs        int32
	Mems        float64
	Latency     float64
	Alpha       float64
}

var modelProfileMap = make(map[string][]IConfigs)
var modelAlphaMap = make(map[string]float64)
var funcShareMem = make(map[string]map[string]float64)

var ProfilesMap = make(map[string]map[string]float64)
var ProfilesMemMap = make(map[string]map[string]float64)
var BatchSet = make(map[string][]int32)
var ConcurrencySet = make(map[string][]int32)
var BatchAndConcurrencySet = make(map[string]map[int32][]CB)

func InitProfilesBatchSet(filepath string, ProfilesBatchSet *[]int32) {
	file, err := os.Open(filepath)
	if err != nil {
		log.Println(err)
		return
	}

	defer file.Close()
	content, err := ioutil.ReadAll(file)
	lines := strings.Split(strings.TrimSpace(string(content)), "\n")
	for _, value := range lines {
		batch, err := strconv.ParseInt(value, 10, 32)
		if err != nil {
			log.Println(err)
			return
		}
		// fmt.Println(batch)
		*ProfilesBatchSet = append(*ProfilesBatchSet, int32(batch))
	}
}

func InitProfilesConcurrencySet(filepath string, ProfilesConcurrencySet *[]int32) {
	file, err := os.Open(filepath)
	if err != nil {
		log.Println(err)
		return
	}

	defer file.Close()
	content, err := ioutil.ReadAll(file)
	lines := strings.Split(strings.TrimSpace(string(content)), "\n")
	for _, value := range lines {
		concurrency, err := strconv.ParseInt(value, 10, 32)
		if err != nil {
			log.Println(err)
			return
		}
		// fmt.Println(batch)
		*ProfilesConcurrencySet = append(*ProfilesConcurrencySet, int32(concurrency))
	}
}

func InitProfiler() {
	maxThroughputEfficiencyMap = make(map[string]float64)
	// resnet50ProfilesMap = make(map[string]float64)
	// catdogProfilesMap = make(map[string]float64)
	// lstm2365ProfilesMap = make(map[string]float64)
	// textcnn69ProfilesMap = make(map[string]float64)
	// mobilenetProfilesMap = make(map[string]float64)
	// ssdProfilesMap = make(map[string]float64)

	// optional
	// AdditionalInit("./yaml/profiler/alpha.txt", modelAlphaMap)

	initFSM("./yaml/profiler/share.txt")

	initModelProfiles(modelProfileMap, "resnet-50-keras", "./yaml/profiler/resnet-50-keras-profile-results-tail.txt", maxThroughputEfficiencyMap, modelAlphaMap)

	// initModel(resnet50kerasProfilesMap, resnet50kerasProfilesMemMap, "resnet-50-keras", "./yaml/profiler/resnet-50-keras-profile-results-tail.txt", maxThroughputEfficiencyMap)

	// InitProfiles("resnet-50-keras", "./yaml/profiler/resnet50-batch.txt", "./yaml/profiler/resnet50-concurrency.txt", "./yaml/profiler/resnet-50-keras-profile-results-tail.txt")

}

func AdditionalInit(filepath string, modelAlphaMap map[string]float64) {
	file, err := os.Open(filepath)
	if err != nil {
		log.Println(err)
		return
	}

	defer file.Close()
	content, err := ioutil.ReadAll(file)
	lines := strings.Split(strings.TrimSpace(string(content)), "\n")
	for _, value := range lines {
		params := strings.Split(value, " ")
		modelname := params[0]
		alpha, err := strconv.ParseFloat(params[1], 64)
		if err != nil {
			log.Println(err)
			return
		}
		modelAlphaMap[modelname] = float64(alpha)
	}
}

func InitProfiles(model_name string, batch_set string, concurrency_set string, configs string) {
	curProfilesMap := make(map[string]float64)
	curProfilesMemMap := make(map[string]float64)
	curProfilesBatchAndConcurrencySet := make(map[int32][]CB)
	batch := []int32{}
	concurrency := []int32{}
	InitProfilesBatchSet(batch_set, &batch)
	InitProfilesConcurrencySet(concurrency_set, &concurrency)
	BatchSet[model_name] = batch
	ConcurrencySet[model_name] = concurrency
	for _, c := range concurrency {
		for _, b := range batch {
			curProfilesBatchAndConcurrencySet[c*b] = append(curProfilesBatchAndConcurrencySet[c*b], CB{c, b})
		}
	}
	initModel(curProfilesMap, curProfilesMemMap, model_name, configs, maxThroughputEfficiencyMap)
	BatchSet[model_name] = batch
	ConcurrencySet[model_name] = concurrency
	BatchAndConcurrencySet[model_name] = curProfilesBatchAndConcurrencySet
	ProfilesMap[model_name] = curProfilesMap
	ProfilesMemMap[model_name] = curProfilesMemMap

}

func initFSM(filePath string) {
	// modelname := "resnet-152-keras"
	// mems := float64(243)
	// if _, ok := funcShareMem[modelname]; !ok {
	// 	funcShareMem[modelname] = map[string]float64{}
	// }
	// if _, ok := funcShareMem[modelname][modelname]; !ok {
	// 	funcShareMem[modelname][modelname] = mems
	// }

	file, err := os.Open(filePath)
	if err != nil {
		log.Println(err)
		return
	}

	defer file.Close()
	content, err := ioutil.ReadAll(file)
	lines := strings.Split(strings.TrimSpace(string(content)), "\n")
	for _, value := range lines {
		params := strings.Split(value, " ")
		modelname1 := params[0]
		modelname2 := params[1]
		smem, err := strconv.ParseFloat(params[2], 64)
		if err != nil {
			log.Println(err)
			// panic(err)
			return
		}
		if _, ok := funcShareMem[modelname1]; !ok {
			funcShareMem[modelname1] = map[string]float64{}
		}
		if _, ok := funcShareMem[modelname1][modelname2]; !ok {
			funcShareMem[modelname1][modelname2] = smem
		}
		if _, ok := funcShareMem[modelname2]; !ok {
			funcShareMem[modelname2] = map[string]float64{}
		}
		if _, ok := funcShareMem[modelname2][modelname1]; !ok {
			funcShareMem[modelname2][modelname1] = smem
		}
	}
}

func initModel(profilesMap map[string]float64, profilesMapMem map[string]float64, modelName, filePath string, maxThroughputEfficiencyMap map[string]float64) {
	file, err := os.Open(filePath)
	if err != nil {
		log.Println(err)
		return
	}

	maxThroughputEfficiency := -1.0
	defer file.Close()
	content, err := ioutil.ReadAll(file)
	lines := strings.Split(strings.TrimSpace(string(content)), "\n")
	for _, value := range lines {
		params := strings.Split(value, " ")
		// log.Println(params)
		cpuCores, _ := strconv.ParseFloat(params[1], 64)
		gpuCores := 0.0
		concurrency, _ := strconv.ParseFloat(params[2], 64)
		batchSize, _ := strconv.ParseFloat(params[3], 64)
		latency, _ := strconv.ParseFloat(params[4], 64)
		memory, _ := strconv.ParseFloat(params[5], 64)
		if err == nil {
			profilesMap[params[1]+"_0_"+params[2]+"_"+params[3]] = latency
			profilesMapMem[params[1]+"_0_"+params[2]+"_"+params[3]] = memory
		}
		tempThroughputEfficiency := 1000 / latency * concurrency * batchSize / (cpuCores*64 + gpuCores*142)
		if tempThroughputEfficiency > maxThroughputEfficiency {
			maxThroughputEfficiency = tempThroughputEfficiency
		}
	}
	maxThroughputEfficiencyMap[modelName] = maxThroughputEfficiency
	if len(profilesMap) > 0 {
		log.Printf("estimator: read model %s profiling data successfully, number of combinations=%d, maxThroughputEfficieny=%f\n", filePath, len(profilesMap), maxThroughputEfficiency)
	}

}

func initModelProfiles(profilesMap map[string][]IConfigs, modelName, filePath string, maxThroughputEfficiencyMap, modelAlphaMap map[string]float64) {
	file, err := os.Open(filePath)
	if err != nil {
		log.Println(err)
		return
	}

	alpha_val := 0.0
	if _, ok := modelAlphaMap[modelName]; ok {
		alpha_val = modelAlphaMap[modelName]
	}

	maxThroughputEfficiency := -1.0
	defer file.Close()
	content, err := ioutil.ReadAll(file)
	lines := strings.Split(strings.TrimSpace(string(content)), "\n")
	for _, value := range lines {
		params := strings.Split(value, " ")
		// log.Println(params)
		cpuCores, _ := strconv.ParseFloat(params[1], 64)
		gpuCores := 0.0
		concurrency, _ := strconv.ParseFloat(params[2], 64)
		batchSize, _ := strconv.ParseFloat(params[3], 64)
		// if batchSize != 1 {
		// 	continue
		// }
		// if cpuCores%concurrency != 0 {
		// 	continue
		// }
		// if int(cpuCores/concurrency) != 2 {
		// 	continue
		// }
		latency, _ := strconv.ParseFloat(params[4], 64)
		memory, _ := strconv.ParseFloat(params[5], 64)

		var ic IConfigs
		ic.Concurrency = int32(concurrency)
		ic.Batch = int32(batchSize)
		ic.CPUs = int32(cpuCores)
		ic.Mems = memory
		ic.Latency = latency
		ic.Alpha = alpha_val
		skey := modelName
		if _, ok := modelProfileMap[skey]; !ok {
			modelProfileMap[skey] = []IConfigs{}
		}
		modelProfileMap[skey] = append(modelProfileMap[skey], ic)
		tempThroughputEfficiency := 1000 / latency * concurrency * batchSize / (cpuCores*64 + gpuCores*142)
		if tempThroughputEfficiency > maxThroughputEfficiency {
			maxThroughputEfficiency = tempThroughputEfficiency
		}
	}
	maxThroughputEfficiencyMap[modelName] = maxThroughputEfficiency
	if len(profilesMap) > 0 {
		log.Printf("estimator: read model %s profiling data successfully, number of combinations=%d, maxThroughputEfficieny=%f\n", filePath, len(profilesMap), maxThroughputEfficiency)
	}

}

type DataSlice struct {
	Data []IConfigs
	By   func(q, p *IConfigs) bool
}

func (d DataSlice) Len() int {
	return len(d.Data)
}

func (d DataSlice) Swap(i, j int) {
	d.Data[i], d.Data[j] = d.Data[j], d.Data[i]
}

func (d DataSlice) Less(i, j int) bool {
	return d.By(&d.Data[i], &d.Data[j])
}

func IC_qps(ic *IConfigs) float64 {
	exec_time := float64(ic.Latency)
	concurrency := float64(ic.Concurrency)
	batch_size := float64(ic.Batch)
	return (1.0 / (exec_time / 1000)) * concurrency * batch_size
}

func IC_cost(ic *IConfigs) float64 {
	cpus := float64(ic.CPUs)
	mems := float64(ic.Mems)
	alpha := float64(ic.Alpha)
	return cpus*alpha + mems
}

func ICEfficiency(ic *IConfigs) float64 {
	ic_qps := IC_qps(ic)
	ic_cost := IC_cost(ic)
	return ic_qps / ic_cost
}

func IC_eff_cmp(p, q *IConfigs) bool {
	p_eff := ICEfficiency(p)
	q_eff := ICEfficiency(q)
	return p_eff > q_eff
}

func IC_Sort(ics *[]IConfigs) {
	sort.Sort(DataSlice{*ics, IC_eff_cmp})
}

// func GetConfigs(modelname string) ([]IConfigs, error) {
// 	if _, ok := modelProfileMap[modelname]; !ok {
// 		return []IConfigs{}, fmt.Errorf("no model %s found in modelProfileMap", modelname)
// 	}
// 	return modelProfileMap[modelname], nil
// }

func getConfigs(modelname string) ([]IConfigs, error) {
	if _, ok := modelProfileMap[modelname]; !ok {
		return []IConfigs{}, fmt.Errorf("no model %s found in modelProfileMap", modelname)
	}
	copyData := make([]IConfigs, len(modelProfileMap[modelname]))
	copy(copyData, modelProfileMap[modelname])
	return copyData, nil
}

func GetFuncShareMem() map[string]map[string]float64 {
	return funcShareMem
}

func inferResourceConfigsWithBatch(funcName string, latencySLO float64, batchSize int32, residualReq int32) (instanceConfig []*gTypes.FuncPodConfig, err error) {
	var availInstConfigs []*gTypes.FuncPodConfig
	/*
		minExecTimeWithGpu := int32(execTimeModel(funcName,1, 2,100))
		minExecTimeOnlyCpu := int32(execTimeModelOnlyCPU(funcName,1, 2))
		if latencySLO < minExecTimeOnlyCpu && latencySLO < minExecTimeWithGpu {
			//log.Printf("estimator: latencySLO %d is too low to be met with minExecTimeOnlyCPU=%d and minExecTimeWithGpu=%d(cpuThread=%d)\n",latencySLO, minExecTimeOnlyCpu, minExecTimeWithGpu, batchSize)
			err = fmt.Errorf("estimator:w latencySLO %d is too low to be met with minExecTimeOnlyCPU=%d and minExecTimeWithGpu=%d(batchSize=%d)\n",latencySLO, minExecTimeOnlyCpu, minExecTimeWithGpu, batchSize)
			return nil, err
		}*/

	sloMeet := false
	timeForExec := latencySLO / 2
	// initCpuThreads := batchSize
	initCpuThreads := int32(8)
	if batchSize == 1 {
		timeForExec = latencySLO
		// initCpuThreads = 2
	}
	//- supportBatchGroup[i]/reqArrivalRate
	//supportCPUthreadsGroup := [...]int32{16,14,12,...,2}
	reqPerSecondMax := int32(0)
	reqPerSecondMin := int32(0)
	batchTimeOut := int32(0)
	for cpuThreads := initCpuThreads; cpuThreads > 0; cpuThreads = cpuThreads - 2 {
		expectTime := getExecTimeModel(funcName, 1, batchSize, cpuThreads, 0)
		if gTypes.LessEqual(expectTime, timeForExec) {
			sloMeet = true
			reqPerSecondMax = int32(1000 / expectTime * float64(batchSize))
			if batchSize == 1 {
				reqPerSecondMin = 1
				batchTimeOut = 0
			} else {
				reqPerSecondMin = int32(1000 / (latencySLO - expectTime) * float64(batchSize))
				batchTimeOut = int32(latencySLO-expectTime) * 1000
				if batchTimeOut < 0 {
					batchTimeOut = 0
				}
			}

			if residualReq >= reqPerSecondMin {
				availInstConfigs = append(availInstConfigs, &gTypes.FuncPodConfig{
					BatchSize:       batchSize,
					CpuThreads:      cpuThreads,
					GpuCorePercent:  0,
					GpuMemoryRate:   -1,
					ExecutionTime:   int32(expectTime),
					BatchTimeOut:    batchTimeOut,
					BatchThreads:    int32(1),
					ReqPerSecondMax: reqPerSecondMax,
					ReqPerSecondMin: reqPerSecondMin,
				})
				//log.Printf("estimator: function=%s, batch=%d, cpuThread=%d, gpuPercent=%d, batchTimeOut=%d, value=%d\n", funcName,batchSize,cpuThreads,int32(0),batchTimeOut,int32(expectTime))
			}
		}
		// for gpuCorePercent := 50; gpuCorePercent > 0; gpuCorePercent = gpuCorePercent - 10 { //gpu cores decreases with 10%
		// 	expectTime = getExecTimeModel(funcName,1,batchSize, cpuThreads, int32(gpuCorePercent))
		// 	if gTypes.LessEqual(expectTime, timeForExec) {
		// 		sloMeet = true
		// 		reqPerSecondMax = int32(1000/expectTime*float64(batchSize)) //no device idle time - queuing time equals execution time
		// 		if batchSize == 1 {
		// 			reqPerSecondMin = 1
		// 			batchTimeOut = 0
		// 		} else {
		// 			reqPerSecondMin = int32(1000/(latencySLO-expectTime)*float64(batchSize))
		// 			batchTimeOut = int32(latencySLO-expectTime)*1000
		// 			if batchTimeOut < 0 {
		// 				batchTimeOut = 0
		// 			}
		// 		}

		// 		if residualReq >= reqPerSecondMin {
		// 			availInstConfigs = append(availInstConfigs, &gTypes.FuncPodConfig {
		// 				BatchSize:      batchSize,
		// 				CpuThreads:     cpuThreads,
		// 				GpuCorePercent: int32(gpuCorePercent),
		// 				GpuMemoryRate: -1,
		// 				ExecutionTime:  int32(expectTime),
		// 				BatchTimeOut: batchTimeOut,
		// 				ReqPerSecondMax: reqPerSecondMax,
		// 				ReqPerSecondMin: reqPerSecondMin,
		// 			})
		// 			//log.Printf("estimator: function=%s, batch=%d, cpuThread=%d, gpuPercent=%d, batchTimeOut=%d, value=%d\n", funcName,batchSize,cpuThreads,int32(gpuCorePercent),batchTimeOut,int32(expectTime))
		// 		}
		// 	}
		// }
	}
	if len(availInstConfigs) > 0 {
		return availInstConfigs, nil
	} else {
		if sloMeet == true {
			err = fmt.Errorf("estimator: residualReq %d is too low to be met with (batchsize=%d)\n", residualReq, batchSize)
		} else {
			err = fmt.Errorf("estimator: latencySLO %f is too low to be met with (batchsize=%d)\n", latencySLO, batchSize)
		}

		return nil, err
	}
}

func inferResourceConfigsWithBatchAndConcurrency(funcName string, latencySLO float64, concurrency int32, batchSize int32, residualReq int32) (instanceConfig []*gTypes.FuncPodConfig, err error) {
	var availInstConfigs []*gTypes.FuncPodConfig
	/*
		minExecTimeWithGpu := int32(execTimeModel(funcName,1, 2,100))
		minExecTimeOnlyCpu := int32(execTimeModelOnlyCPU(funcName,1, 2))
		if latencySLO < minExecTimeOnlyCpu && latencySLO < minExecTimeWithGpu {
			//log.Printf("estimator: latencySLO %d is too low to be met with minExecTimeOnlyCPU=%d and minExecTimeWithGpu=%d(cpuThread=%d)\n",latencySLO, minExecTimeOnlyCpu, minExecTimeWithGpu, batchSize)
			err = fmt.Errorf("estimator:w latencySLO %d is too low to be met with minExecTimeOnlyCPU=%d and minExecTimeWithGpu=%d(batchSize=%d)\n",latencySLO, minExecTimeOnlyCpu, minExecTimeWithGpu, batchSize)
			return nil, err
		}*/

	sloMeet := false
	timeForExec := latencySLO / 2
	// initCpuThreads := batchSize
	initCpuThreads := int32(8)
	if batchSize == 1 {
		timeForExec = latencySLO
		// initCpuThreads = 2
	}
	//- supportBatchGroup[i]/reqArrivalRate
	//supportCPUthreadsGroup := [...]int32{16,14,12,...,2}
	reqPerSecondMax := int32(0)
	reqPerSecondMin := int32(0)
	batchTimeOut := int32(0)
	for cpuThreads := initCpuThreads; cpuThreads > 0; cpuThreads = cpuThreads - 2 {
		// if cpuThreads%concurrency != 0 {
		// 	continue
		// }
		// if int(cpuThreads/concurrency) != 2 {
		// 	continue
		// }
		expectTime := getExecTimeModel(funcName, concurrency, batchSize, cpuThreads, 0)
		if gTypes.LessEqual(expectTime, timeForExec) {
			sloMeet = true
			reqPerSecondMax = int32(1000 / expectTime * float64(batchSize) * float64(concurrency))
			if batchSize == 1 {
				reqPerSecondMin = concurrency
				batchTimeOut = 0
			} else {
				reqPerSecondMin = int32(1000 / (latencySLO - expectTime) * float64(batchSize) * float64(concurrency))
				batchTimeOut = int32(latencySLO-expectTime) * 1000
				if batchTimeOut < 0 {
					batchTimeOut = 0
				}
			}

			// if residualReq >= reqPerSecondMin {
			// 	availInstConfigs = append(availInstConfigs, &gTypes.FuncPodConfig {
			// 		BatchSize:      batchSize,
			// 		CpuThreads:     cpuThreads,
			// 		GpuCorePercent: 0,
			// 		GpuMemoryRate: -1,
			// 		ExecutionTime:  int32(expectTime),
			// 		BatchTimeOut: batchTimeOut,
			// 		BatchThreads: concurrency,
			// 		ReqPerSecondMax: reqPerSecondMax,
			// 		ReqPerSecondMin: reqPerSecondMin,
			// 	})
			// 	//log.Printf("estimator: function=%s, batch=%d, cpuThread=%d, gpuPercent=%d, batchTimeOut=%d, value=%d\n", funcName,batchSize,cpuThreads,int32(0),batchTimeOut,int32(expectTime))
			// }

			availInstConfigs = append(availInstConfigs, &gTypes.FuncPodConfig{
				BatchSize:       batchSize,
				CpuThreads:      cpuThreads,
				GpuCorePercent:  0,
				GpuMemoryRate:   -1,
				ExecutionTime:   int32(expectTime),
				BatchTimeOut:    batchTimeOut,
				BatchThreads:    concurrency,
				ReqPerSecondMax: reqPerSecondMax,
				ReqPerSecondMin: reqPerSecondMin,
			})
			//log.Printf("estimator: function=%s, batch=%d, cpuThread=%d, gpuPercent=%d, batchTimeOut=%d, value=%d\n", funcName,batchSize,cpuThreads,int32(0),batchTimeOut,int32(expectTime))

		}
		// for gpuCorePercent := 50; gpuCorePercent > 0; gpuCorePercent = gpuCorePercent - 10 { //gpu cores decreases with 10%
		// 	expectTime = getExecTimeModel(funcName,1,batchSize, cpuThreads, int32(gpuCorePercent))
		// 	if gTypes.LessEqual(expectTime, timeForExec) {
		// 		sloMeet = true
		// 		reqPerSecondMax = int32(1000/expectTime*float64(batchSize)) //no device idle time - queuing time equals execution time
		// 		if batchSize == 1 {
		// 			reqPerSecondMin = 1
		// 			batchTimeOut = 0
		// 		} else {
		// 			reqPerSecondMin = int32(1000/(latencySLO-expectTime)*float64(batchSize))
		// 			batchTimeOut = int32(latencySLO-expectTime)*1000
		// 			if batchTimeOut < 0 {
		// 				batchTimeOut = 0
		// 			}
		// 		}

		// 		if residualReq >= reqPerSecondMin {
		// 			availInstConfigs = append(availInstConfigs, &gTypes.FuncPodConfig {
		// 				BatchSize:      batchSize,
		// 				CpuThreads:     cpuThreads,
		// 				GpuCorePercent: int32(gpuCorePercent),
		// 				GpuMemoryRate: -1,
		// 				ExecutionTime:  int32(expectTime),
		// 				BatchTimeOut: batchTimeOut,
		// 				ReqPerSecondMax: reqPerSecondMax,
		// 				ReqPerSecondMin: reqPerSecondMin,
		// 			})
		// 			//log.Printf("estimator: function=%s, batch=%d, cpuThread=%d, gpuPercent=%d, batchTimeOut=%d, value=%d\n", funcName,batchSize,cpuThreads,int32(gpuCorePercent),batchTimeOut,int32(expectTime))
		// 		}
		// 	}
		// }
	}
	if len(availInstConfigs) > 0 {
		return availInstConfigs, nil
	} else {
		if sloMeet == true {
			err = fmt.Errorf("estimator: residualReq %d is too low to be met with (batchsize=%d)\n", residualReq, batchSize)
		} else {
			err = fmt.Errorf("estimator: latencySLO %f is too low to be met with (batchsize=%d)\n", latencySLO, batchSize)
		}

		return nil, err
	}
}

func inferResourceConfigsWithBatchAndConcurrencyAll(funcName string, cb_list *[]IConfigs, latencySLO float64, residualReq int32) (instanceConfig []*gTypes.FuncPodConfig, err error) {
	var availInstConfigs []*gTypes.FuncPodConfig
	/*
		minExecTimeWithGpu := int32(execTimeModel(funcName,1, 2,100))
		minExecTimeOnlyCpu := int32(execTimeModelOnlyCPU(funcName,1, 2))
		if latencySLO < minExecTimeOnlyCpu && latencySLO < minExecTimeWithGpu {
			//log.Printf("estimator: latencySLO %d is too low to be met with minExecTimeOnlyCPU=%d and minExecTimeWithGpu=%d(cpuThread=%d)\n",latencySLO, minExecTimeOnlyCpu, minExecTimeWithGpu, batchSize)
			err = fmt.Errorf("estimator:w latencySLO %d is too low to be met with minExecTimeOnlyCPU=%d and minExecTimeWithGpu=%d(batchSize=%d)\n",latencySLO, minExecTimeOnlyCpu, minExecTimeWithGpu, batchSize)
			return nil, err
		}*/

	// sloMeet := false

	for _, cb := range *cb_list {
		batchSize := cb.Batch
		timeForExec := latencySLO / 2

		if batchSize == 1 {
			timeForExec = latencySLO
		}
		reqPerSecondMax := int32(0)
		reqPerSecondMin := int32(0)
		batchTimeOut := int32(0)

		expectTime := cb.Latency
		concurrency := cb.Concurrency

		if gTypes.LessEqual(expectTime, timeForExec) {
			// sloMeet = true
			reqPerSecondMax = int32(1000 / expectTime * float64(batchSize) * float64(concurrency)) //no device idle time - queuing time equals execution time
			if batchSize == 1 {
				reqPerSecondMin = concurrency
				batchTimeOut = 0
			} else {
				reqPerSecondMin = int32(1000 / (latencySLO - expectTime) * float64(batchSize) * float64(concurrency))
				batchTimeOut = int32(latencySLO-expectTime) * 1000
				if batchTimeOut < 0 {
					batchTimeOut = 0
				}
			}

			cpuThreads := cb.CPUs

			availInstConfigs = append(availInstConfigs, &gTypes.FuncPodConfig{
				BatchSize:       batchSize,
				CpuThreads:      cpuThreads,
				GpuCorePercent:  0,
				GpuMemoryRate:   -1,
				ExecutionTime:   int32(expectTime),
				BatchTimeOut:    batchTimeOut,
				BatchThreads:    concurrency,
				ReqPerSecondMax: reqPerSecondMax,
				ReqPerSecondMin: reqPerSecondMin,
			})
			break
		}
	}

	if len(availInstConfigs) > 0 {
		return availInstConfigs, nil
	} else {
		err = fmt.Errorf("estimator: latencySLO %f is too low\n", latencySLO)
		return nil, err
	}
}

//func execTimeModel(funcName string, batchSize int32, cpuThread int32, gpuCorePercent int32) float64{
//	if funcName == "resnet50" {
//		b := float64(batchSize)
//		t := float64(cpuThread)
//		g := float64(gpuCorePercent) / 100
//		return float64(1423)*b/(t+40.09)*math.Pow(g,-0.3105)+17.92
//	}
//	log.Printf("estimator: could find exection time model for function %s", funcName)
//	return 99999
//}
//func execTimeModelOnlyCPU(funcName string, batchSize int32, cpuThread int32) float64{
//	if funcName == "resnet50" {
//		b := float64(batchSize)
//		t := float64(cpuThread)
//		return 34.76*b/(math.Pow(t,0.341)-0.9926)+69.87+150
//	}
//	log.Printf("estimator: could find exection time model(only CPU) for function %s", funcName)
//	return 99999
//
//}

func getExecTimeModel(funcName string, concurrency int32, batchSize int32, cpuThread int32, gpuCorePercent int32) float64 {
	if int(gpuCorePercent) != 0 {
		panic("getExecTimeModel: gpu non-zero")
	}
	// key := strconv.Itoa(int(cpuThread)) + "_" + strconv.Itoa(int(gpuCorePercent)) + "_" + strconv.Itoa(int(batchSize))
	key := strconv.Itoa(int(cpuThread)) + "_" + strconv.Itoa(0) + "_" + strconv.Itoa(int(concurrency)) + "_" + strconv.Itoa(int(batchSize))
	log.Printf(key)
	value := float64(0)
	ok := false

	if pmap, okk := ProfilesMap[funcName]; okk {
		value, ok = pmap[key]
	}

	if ok {
		// log.Printf("estimator: function=%s, batch=%d, cpuThread=%d, gpuPercent=%d, value=%f", funcName, batchSize, cpuThread, gpuCorePercent, value)
		return value
	} else {
		//log.Printf("estimator: could not find exection time model for function=%s, batch=%d, cpuThread=%d, gpuPercent=%d", funcName,batchSize,cpuThread,gpuCorePercent)
		return 99999
	}
}

// func getExecTimeModel(funcName string, concurrency int32, batchSize int32, cpuThread int32, gpuCorePercent int32) float64 {
// 	// cpuCores gpuCores concurrency batchSize
// 	if int(gpuCorePercent) != 0 {
// 		panic("getExecTimeModel: gpu non-zero")
// 	}
// 	// key := strconv.Itoa(int(cpuThread)) + "_" + strconv.Itoa(int(gpuCorePercent)) + "_" + strconv.Itoa(int(batchSize))
// 	key := strconv.Itoa(int(cpuThread)) + "_" + strconv.Itoa(0) + "_" + strconv.Itoa(int(concurrency)) + "_" + strconv.Itoa(int(batchSize))
// 	log.Printf(key)
// 	value := float64(0)
// 	ok := false
// 	// if funcName == "resnet-50" {
// 	// 	value, ok = resnet50ProfilesMap[key]
// 	// } else if funcName == "catdog" {
// 	// 	value, ok = catdogProfilesMap[key]
// 	// } else if funcName == "lstm-maxclass-2365" {
// 	// 	value, ok = lstm2365ProfilesMap[key]
// 	// } else if funcName == "textcnn-69" {
// 	// 	value, ok = textcnn69ProfilesMap[key]
// 	// } else if funcName == "mobilenet" {
// 	// 	value, ok = mobilenetProfilesMap[key]
// 	// } else if funcName == "ssd" {
// 	// 	value, ok = ssdProfilesMap[key]
// 	// }
// 	if funcName == "resnet-50-keras" { // lijie
// 		value, ok = resnet50kerasProfilesMap[key]
// 	} else if funcName == "resnet-152-keras" {
// 		value, ok = resnet152kerasProfilesMap[key]
// 	} else if funcName == "use-qa" {
// 		value, ok = useqaProfilesMap[key]
// 	} else if funcName == "universal-sentence-encoder-large" {
// 		value, ok = useProfilesMap[key]
// 	} else if funcName == "lstm-maxclass-2365" {
// 		value, ok = lstmProfilesMap[key]
// 	}
// 	if ok {
// 		log.Printf("estimator: function=%s, batch=%d, cpuThread=%d, gpuPercent=%d, value=%f", funcName, batchSize, cpuThread, gpuCorePercent, value)
// 		return value
// 	} else {
// 		//log.Printf("estimator: could not find exection time model for function=%s, batch=%d, cpuThread=%d, gpuPercent=%d", funcName,batchSize,cpuThread,gpuCorePercent)
// 		return 99999
// 	}
// }

func getBatchSet(funcName string) ([]int32, error) {
	// ok := false
	if bs, okk := BatchSet[funcName]; okk {
		return bs, nil
	}
	return []int32{}, errors.New("getBatchSet " + funcName + " Error")
}

// func getBatchSet(funcName string) ([]int32, error) {
// 	// ok := false
// 	if funcName == "resnet-50-keras" { // lijie
// 		return resnet50kerasProfilesBatchSet, nil
// 	} else if funcName == "resnet-152-keras" {
// 		return resnet152kerasProfilesBatchSet, nil
// 	} else if funcName == "use-qa" {
// 		return useqaProfilesBatchSet, nil
// 	} else if funcName == "universal-sentence-encoder-large" {
// 		return useProfilesBatchSet, nil
// 	} else if funcName == "lstm-maxclass-2365" {
// 		return lstmProfilesBatchSet, nil
// 	}
// 	return []int32{}, errors.New("getBatchSet " + funcName + " Error")
// }

func getConcurrencySet(funcName string) ([]int32, error) {
	// ok := false
	if cs, okk := ConcurrencySet[funcName]; okk {
		return cs, nil
	}
	return []int32{}, errors.New("getConcurrencySet " + funcName + " Error")
}

// func getConcurrencySet(funcName string) ([]int32, error) {
// 	// ok := false
// 	if funcName == "resnet-50-keras" { // lijie
// 		return resnet50kerasProfilesConcurrencySet, nil
// 	} else if funcName == "resnet-152-keras" {
// 		return resnet152kerasProfilesConcurrencySet, nil
// 	} else if funcName == "use-qa" {
// 		return useqaProfilesConcurrencySet, nil
// 	} else if funcName == "universal-sentence-encoder-large" {
// 		return useProfilesConcurrencySet, nil
// 	} else if funcName == "lstm-maxclass-2365" {
// 		return lstmProfilesConcurrencySet, nil
// 	}
// 	return []int32{}, errors.New("getConcurrencySet " + funcName + " Error")
// }

func getBatchAndConcurrencySet(funcName string) (map[int32][]CB, error) {
	// ok := false
	if bcs, okk := BatchAndConcurrencySet[funcName]; okk {
		return bcs, nil
	}
	return nil, errors.New("getBatchAndConcurrencySet " + funcName + " Error")
}

/**
 * query the maximum throughput efficiency of a function
 */
func getMaxThroughputEfficiency(funcName string) float64 {
	value, ok := maxThroughputEfficiencyMap[funcName]
	if ok {
		//log.Printf("estimator: function=%s, batch=%d, cpuThread=%d, gpuPercent=%d, value=%f", funcName,batchSize,cpuThread,gpuCorePercent,value)
		return value
	} else {
		//log.Printf("estimator: could not find exection time model for function=%s, batch=%d, cpuThread=%d, gpuPercent=%d", funcName,batchSize,cpuThread,gpuCorePercent)
		return 99999
	}
}
