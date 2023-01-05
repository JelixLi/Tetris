// Copyright (c) Alex Ellis 2017. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

package main

import (
	"flag"
	cpuRepository "github.com/openfaas/faas-netes/cpu/repository"
	"github.com/openfaas/faas-netes/gpu/aside"
	 "github.com/openfaas/faas-netes/gpu/repository"
	"log"
	"os"
	"time"
        scheduler "github.com/openfaas/faas-netes/gpu/controller"

	"github.com/openfaas/faas-provider/proxy"
	"k8s.io/client-go/kubernetes"

	"github.com/openfaas-incubator/openfaas-operator/pkg/signals"
	"github.com/openfaas/faas-netes/handlers"
	"github.com/openfaas/faas-netes/k8s"
	"github.com/openfaas/faas-netes/types"
	"github.com/openfaas/faas-netes/version"
	bootstrap "github.com/openfaas/faas-provider"
	"github.com/openfaas/faas-provider/logs"
	bootTypes "github.com/openfaas/faas-provider/types"
	kubeinformers "k8s.io/client-go/informers"

	"k8s.io/client-go/tools/clientcmd"
)

func main() {
        time.Local = time.FixedZone("CST", 3600*8)
       	log.SetFlags(log.Lmicroseconds)
	repository.Init()
        scheduler.InitProfiler()

	var kubeconfig string
	var masterURL string

	flag.StringVar(&kubeconfig, "kubeconfig", "",
		"Path to a kubeconfig. Only required if out-of-cluster.")
	flag.StringVar(&masterURL, "master", "",
		"The address of the Kubernetes API server. Overrides any value in kubeconfig. Only required if out-of-cluster.")
	flag.Parse()

	clientCmdConfig, err := clientcmd.BuildConfigFromFlags(masterURL, kubeconfig)
	if err != nil {
		log.Fatalf("Error building kubeconfig: %s", err.Error())
	}

	clientset, err := kubernetes.NewForConfig(clientCmdConfig)
	if err != nil {
		log.Fatalf("Error building Kubernetes clientset: %s", err.Error())

	}

	functionNamespace := "default"

	if namespace, exists := os.LookupEnv("function_namespace"); exists {
		functionNamespace = namespace
	}

	readConfig := types.ReadConfig{}
	osEnv := bootTypes.OsEnv{}
	cfg, err := readConfig.Read(osEnv)
	if err != nil {
		log.Fatalf("Error reading config: %s", err.Error())
	}

	log.Printf("HTTP Read Timeout: %s\n", cfg.FaaSConfig.GetReadTimeout())
	log.Printf("HTTP Write Timeout: %s\n", cfg.FaaSConfig.WriteTimeout)
	log.Printf("HTTPProbe: %v\n", cfg.HTTPProbe)
	log.Printf("SetNonRootUser: %v\n", cfg.SetNonRootUser)

	deployConfig := k8s.DeploymentConfig {
		RuntimeHTTPPort: 8080,
		HTTPProbe:       cfg.HTTPProbe,
		SetNonRootUser:  cfg.SetNonRootUser,
		ReadinessProbe: &k8s.ProbeConfig{
			InitialDelaySeconds: int32(cfg.ReadinessProbeInitialDelaySeconds),
			TimeoutSeconds:      int32(cfg.ReadinessProbeTimeoutSeconds),
			PeriodSeconds:       int32(cfg.ReadinessProbePeriodSeconds),
		},
		LivenessProbe: &k8s.ProbeConfig{
			InitialDelaySeconds: int32(cfg.LivenessProbeInitialDelaySeconds),
			TimeoutSeconds:      int32(cfg.LivenessProbeTimeoutSeconds),
			PeriodSeconds:       int32(cfg.LivenessProbePeriodSeconds),
		},
		ImagePullPolicy: cfg.ImagePullPolicy,
	}

	factory := k8s.NewFunctionFactory(clientset, deployConfig)

	defaultResync := time.Second * 5
	kubeInformerOpt := kubeinformers.WithNamespace(functionNamespace)
	kubeInformerFactory := kubeinformers.NewSharedInformerFactoryWithOptions(clientset, defaultResync, kubeInformerOpt)

	// set up signals so we handle the first shutdown signal gracefully
	stopCh := signals.SetupSignalHandler()

	endpointsInformer := kubeInformerFactory.Core().V1().Endpoints()
	go kubeInformerFactory.Start(stopCh)
	lister := endpointsInformer.Lister()

	functionLookup := k8s.NewFunctionLookup(functionNamespace, lister)

	bootstrapHandlers := bootTypes.FaaSHandlers {
		FunctionProxy:        proxy.NewHandlerFunc(cfg.FaaSConfig, functionLookup),
		DeleteHandler:        handlers.MakeDeleteHandler(functionNamespace, clientset),
		DeployHandler:        handlers.MakeDeployHandler(functionNamespace, factory, clientset),
		FunctionReader:       handlers.MakeFunctionReader(functionNamespace, clientset),
		ReplicaReader:        handlers.MakeReplicaReader(functionNamespace, clientset),
		ReplicaUpdater:       handlers.MakeReplicaUpdater(functionNamespace, clientset),
		UpdateHandler:        handlers.MakeUpdateHandler(functionNamespace, factory),
		HealthHandler:        handlers.MakeHealthHandler(),
		InfoHandler:          handlers.MakeInfoHandler(version.BuildVersion(), version.GitCommit),
		SecretHandler:        handlers.MakeSecretHandler(functionNamespace, clientset),
		LogHandler:           logs.NewLogHandlerFunc(k8s.NewLogRequestor(clientset, functionNamespace), cfg.FaaSConfig.WriteTimeout),
		ListNamespaceHandler: handlers.MakeNamespacesLister(functionNamespace, clientset),
	}
        go aside.RpsDispatcherMonitor(functionNamespace, cfg.LoadGenHost, cfg.LoadGenPort)
	cpuRepository.InitializeCluster(clientset)
	bootstrap.Serve(&bootstrapHandlers, &cfg.FaaSConfig)
}

//
//package main
//
//import (
//	"flag"
//	"fmt"
//	"github.com/openfaas/faas-netes/gpu/repository"
//	ptypes "github.com/openfaas/faas-provider/types"
//	"math/rand"
//	"sync"
//
//	"time"
//
//	//"github.com/openfaas/faas-netes/gpu/aside"
//	scheduler "github.com/openfaas/faas-netes/gpu/controller"
//	//"github.com/openfaas/faas-netes/gpu/repository"
//	"github.com/openfaas/faas-netes/gpu/tools"
//	gpuTypes "github.com/openfaas/faas-netes/gpu/types"
//	"github.com/openfaas/faas-netes/k8s"
//	types "github.com/openfaas/faas-netes/types"
//	bootTypes "github.com/openfaas/faas-provider/types"
//	corev1 "k8s.io/api/core/v1"
//	"k8s.io/apimachinery/pkg/api/errors"
//	"k8s.io/apimachinery/pkg/api/resource"
//	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
//	"k8s.io/apimachinery/pkg/labels"
//	"k8s.io/apimachinery/pkg/util/intstr"
//	"k8s.io/client-go/kubernetes"
//	"k8s.io/client-go/tools/clientcmd"
//	"log"
//	"net/http"
//	"sort"
//	"strconv"
//	"strings"
//	//"time"
//
//	//"time"
//)
//
//func main() {
//
//	repository.Init()
//
//	var kubeconfig string
//	var masterURL string
//
//	flag.StringVar(&kubeconfig, "kubeconfig", "C:/Go/workspace/k8s/config",
//		"Path to a kubeconfig. Only required if out-of-cluster.")
//	flag.StringVar(&masterURL, "master", "",
//		"The address of the Kubernetes API server. Overrides any value in kubeconfig. Only required if out-of-cluster.")
//	flag.Parse()
//
//	clientCmdConfig, err := clientcmd.BuildConfigFromFlags(masterURL, kubeconfig)
//	if err != nil {
//		log.Fatalf("Error building kubeconfig: %s", err.Error())
//	}
//
//	clientset, err := kubernetes.NewForConfig(clientCmdConfig)
//	if err != nil {
//		panic(err.Error())
//	}
//	readConfig := types.ReadConfig{}
//	osEnv := bootTypes.OsEnv{}
//	cfg, err := readConfig.Read(osEnv)
//	if err != nil {
//		log.Fatalf("Error reading config: %s", err.Error())
//	}
//
//	cfg.HTTPProbe = true
//
//	deployConfig := k8s.DeploymentConfig {
//		RuntimeHTTPPort: 8080,
//		HTTPProbe:       cfg.HTTPProbe,
//		SetNonRootUser:  cfg.SetNonRootUser,
//		ReadinessProbe: &k8s.ProbeConfig {
//			InitialDelaySeconds: int32(cfg.ReadinessProbeInitialDelaySeconds),
//			TimeoutSeconds:      int32(cfg.ReadinessProbeTimeoutSeconds),
//			PeriodSeconds:       int32(cfg.ReadinessProbePeriodSeconds),
//		},
//		LivenessProbe: &k8s.ProbeConfig {
//			InitialDelaySeconds: int32(cfg.LivenessProbeInitialDelaySeconds),
//			TimeoutSeconds:      int32(cfg.LivenessProbeTimeoutSeconds),
//			PeriodSeconds:       int32(cfg.LivenessProbePeriodSeconds),
//		},
//		ImagePullPolicy: cfg.ImagePullPolicy,
//	}
//
//	factory := k8s.NewFunctionFactory(clientset, deployConfig)
//	fmt.Println("factory:", factory)
//
//	//aside.RpsDispatcherMonitor()
//	testScheduler()
//	//testDeploy(clientset, factory)
//	//
//	////testReader("openfaas-fn","sleep", clientset)
//	//testReaderList("test",clientset)
//	//time.Sleep(time.Second*120)
//	//testDelete("test","sleep",clientset)
//
//	//time.Sleep(time.Second*4)
//
//	//cpuRepository.InitializeCluster(clientset)
//
//	/*fn := []string{"sleep","sleep2"}
//	for i:=0; i< len(fn); i++ {
//		aside.ProfileFunc(fn[i], time.Second*60)
//	}
//	time.Sleep(time.Second*100)*/
//	//testReader("openfaas-fn","sleep", clientset)
//	//testReaderList("openfaas-fn", clientset)
//	/*alert.AlertMonitor(clientset, time.Second*10)
//
//	testDeploy(clientset, factory)
//	repository.PrintClusterCapConfig()
//	repository.PrintFuncDeployStatusMap()
//	time.Sleep(time.Second*60)
//	repository.UpdateFuncExpectedReplicas("sleep",5)
//	repository.PrintClusterCapConfig()
//	repository.PrintFuncDeployStatusMap()
//
//	testReplicas("test","sleep", clientset)
//	repository.PrintClusterCapConfig()
//	repository.PrintFuncDeployStatusMap()
//	time.Sleep(time.Second*60)
//	testReader("test","sleep", clientset)
//
//	repository.UpdateFuncExpectedReplicas("sleep",2)
//	repository.PrintClusterCapConfig()
//	repository.PrintFuncDeployStatusMap()
//	testReplicas("test","sleep", clientset)
//	repository.PrintClusterCapConfig()
//	repository.PrintFuncDeployStatusMap()
//	time.Sleep(time.Second*600)
//	//testReader("test","sleep", clientset)
//	testDelete("test","sleep",clientset)
//	repository.PrintClusterCapConfig()
//	repository.PrintFuncDeployStatusMap()*/
//}
//func testScheduler(){
//	type Config struct {
//		Lottary int32
//		MinReq int32
//		MaxReq int32
//		PodName string
//	}
//	type Func struct {
//		MinReqCap int32
//		MaxReqCap int32
//		LottarySum int32
//		ConfigMap map[string]*Config
//	}
//	funcConfig := map[string]*Config{}
//	funcConfig["pod1"]= &Config{
//		Lottary: 75,
//		MinReq:  60,
//		MaxReq:  100,
//		PodName: "pod1",
//	}
//	funcConfig["pod2"]= &Config{
//		Lottary: 155,
//		MinReq:  150,
//		MaxReq:  200,
//		PodName: "pod2",
//	}
//	funcConfig["pod3"]= &Config{
//		Lottary: 55,
//		MinReq:  180,
//		MaxReq:  320,
//		PodName: "pod3",
//	}
//	funcConfig["pod4"]= &Config{
//		Lottary: 254,
//		MinReq:  400,
//		MaxReq:  500,
//		PodName: "pod4",
//	}
//	funcConfig["v"]= &Config{
//		Lottary: 10,
//		MinReq:  0,
//		MaxReq:  0,
//		PodName: "v",
//	}
//	var funcStatuss Func
//	funcStatuss.ConfigMap = funcConfig
//	funcStatuss.MaxReqCap = 1120
//	funcStatuss.MinReqCap = 790
//	funcStatuss.LottarySum = 75+155+254
//
//	type Combine struct {
//		Num int32
//		MinCap int32
//		MaxCap int32
//		DeletePodNameList []string
//		RemainPodNameList []string
//	}
//
//	var letter []*Config
//	for kk, vv := range funcStatuss.ConfigMap {
//		if kk != "v" {
//			letter = append(letter, vv)
//		}
//	}
//
//	var comList []Combine
//
//	n := uint(len(letter))
//	var maxCount uint = 1 << n
//	fmt.Println("maxCount=",maxCount," n=",n)
//	var i uint
//	var j uint
//	for i = 1; i < maxCount-1; i++ {
//		num := int32(0)
//		minSum := int32(0)
//		maxSum := int32(0)
//		var deletePodNameList []string
//		var remainPodNameList []string
//		for j = 0; j < n; j++ {
//			if (i & (1 << j)) != 0 { //在做位运算的时候需要注意数据类型为uint
//				//
//				num++
//				minSum+=letter[j].MinReq
//				maxSum+=letter[j].MaxReq
//				deletePodNameList = append(deletePodNameList, letter[j].PodName)
//				fmt.Printf("%d",j)
//			} else {
//				remainPodNameList = append(remainPodNameList, letter[j].PodName)
//			}
//		}
//		com := Combine{
//			Num:    num,
//			MinCap: minSum,
//			MaxCap: maxSum,
//			DeletePodNameList: deletePodNameList,
//			RemainPodNameList: remainPodNameList,
//		}
//		comList = append(comList, com)
//		fmt.Println()
//	}
//	fmt.Println(comList)
//	fmt.Println(len(comList))
//
//	//reqList := []int32{780,740,650,600,458,390,370,355,350,330,310,300,250,230,200,150,100,50,45,5,0}
//	reqList := []int32{7, 0}
//	for ii:=0;ii<len(reqList);ii++ {
//		curReq := reqList[ii]
//		foundFlag := false
//		candidateNum := int32(n)-1
//		for i := candidateNum; i > 0 && foundFlag == false; i-- {
//			//fmt.Println(111)
//			for j:= 0; j< len(comList); j++ {
//				//fmt.Println(222)
//				if comList[j].Num == int32(i) {
//					tempFuncCapMin := funcStatuss.MinReqCap-comList[j].MinCap
//					tempFuncCapMax := funcStatuss.MaxReqCap-comList[j].MaxCap
//					if curReq >= tempFuncCapMin && curReq <= tempFuncCapMax {
//						for k := 0; k < len(comList[j].DeletePodNameList); k++ {
//							funcStatuss.ConfigMap[comList[j].DeletePodNameList[k]].Lottary = 0
//						}
//						for k := 0; k < len(comList[j].RemainPodNameList); k++ {
//							scalingFactor := float32(funcStatuss.ConfigMap[comList[j].RemainPodNameList[k]].MaxReq-funcStatuss.ConfigMap[comList[j].RemainPodNameList[k]].MinReq)/float32(tempFuncCapMax-tempFuncCapMin)
//							funcStatuss.ConfigMap[comList[j].RemainPodNameList[k]].Lottary = funcStatuss.ConfigMap[comList[j].RemainPodNameList[k]].MaxReq-int32(scalingFactor*float32(tempFuncCapMax-int32(curReq)))
//						}
//						foundFlag = true
//						break
//					}
//				}
//			}
//		}
//		if foundFlag == false {
//			fmt.Println()
//			fmt.Printf("%d faild to find one pod\n",curReq)
//
//		}else {
//			fmt.Println()
//			fmt.Printf("%d \n",curReq)
//			for _, v:= range funcStatuss.ConfigMap {
//				fmt.Printf("%s, lottary=%d \n",v.PodName, v.Lottary)
//			}
//		}
//
//	}
//}
//
//// NewFunctionScaler create a new scaler with the specified
//// ScalingConfig
//type ScalingConfig struct {
//	// MaxPollCount attempts to query a function before giving up
//	MaxPollCount uint
//
//	// FunctionPollInterval delay or interval between polling a function's
//	// readiness status
//	FunctionPollInterval time.Duration
//
//	// CacheExpiry life-time for a cache entry before considering invalid
//	CacheExpiry time.Duration
//
//	// ServiceQuery queries available/ready replicas for function
//
//	// SetScaleRetries is the number of times to try scaling a function before
//	// giving up due to errors
//	SetScaleRetries uint
//}
//
//func NewFunctionScaler(config ScalingConfig) FunctionScaler {
//	cache := FunctionCache {
//		Cache:  make(map[string]*FunctionMeta),
//		Expiry: config.CacheExpiry,
//	}
//	cacheUpdateFlag := FunctionCacheUpdateFlag{
//		CacheUpdate: make(map[string]bool),
//		Sync:        sync.RWMutex{},
//	}
//
//	return FunctionScaler {
//		Cache:  &cache,
//		Config: config,
//		CacheUpdateFlag: &cacheUpdateFlag,
//	}
//}
//type FunctionCache struct {
//	Cache  map[string]*FunctionMeta
//	Expiry time.Duration
//	Sync   sync.RWMutex
//}
//type FunctionCacheUpdateFlag struct {
//	CacheUpdate  map[string]bool
//	Sync   sync.RWMutex
//}
//type FunctionMeta struct {
//	LastRefresh          time.Time
//	ServiceQueryResponse ServiceQueryResponse
//}
//type ServiceQueryResponse struct {
//	Replicas          uint64
//	MaxReplicas       uint64
//	MinReplicas       uint64
//	ScalingFactor     uint64
//	AvailableReplicas uint64
//}
//
//// FunctionScaler scales from zero
//type FunctionScaler struct {
//	CacheUpdateFlag *FunctionCacheUpdateFlag
//	Cache  *FunctionCache
//	Config ScalingConfig
//}
//
//// FunctionScaleResult holds the result of scaling from zero
//type FunctionScaleResult struct {
//	Available bool
//	Error     error
//	Found     bool
//	Duration  time.Duration
//}
//
//
//func (fc *FunctionCache) Set(funcNameKey string, serviceQueryResponse ServiceQueryResponse) {
//	fc.Sync.Lock()
//	defer fc.Sync.Unlock()
//
//	if _, exists := fc.Cache[funcNameKey]; !exists {
//		fc.Cache[funcNameKey] = &FunctionMeta{}
//	}
//
//	fc.Cache[funcNameKey].LastRefresh = time.Now()
//	fc.Cache[funcNameKey].ServiceQueryResponse = serviceQueryResponse
//	// entry.LastRefresh = time.Now()
//	// entry.ServiceQueryResponse = serviceQueryResponse
//}
//
//
//// Get replica count for functionName
//func (fc *FunctionCache) Get(funcNameKey string) (ServiceQueryResponse, bool) {
//	replicas := ServiceQueryResponse{
//		AvailableReplicas: 0,
//	}
//
//	hit := false
//	fc.Sync.RLock()
//	defer fc.Sync.RUnlock()
//
//	if val, exists := fc.Cache[funcNameKey]; exists {
//		replicas = val.ServiceQueryResponse
//		hit = !val.Expired(fc.Expiry)
//	}
//
//	return replicas, hit
//}
//
//// Set replica count for functionName
//func (fcuf *FunctionCacheUpdateFlag) SetFlag(funcNameKey string, flag bool) {
//	fcuf.Sync.Lock()
//	defer fcuf.Sync.Unlock()
//	fcuf.CacheUpdate[funcNameKey] = flag
//}
//
//// Get replica count for functionName
//func (fcuf *FunctionCacheUpdateFlag) GetFlag(funcNameKey string) (bool, bool) {
//	fcuf.Sync.RLock()
//	defer fcuf.Sync.RUnlock()
//
//	val, exists := fcuf.CacheUpdate[funcNameKey]
//
//	return val, exists
//}
//func (fm *FunctionMeta) Expired(expiry time.Duration) bool {
//	return time.Now().After(fm.LastRefresh.Add(expiry))
//}
//func  (f *FunctionScaler) Scale(functionName, namespace string, user int) FunctionScaleResult {
//	start := time.Now()
//	funcNameKey := functionName+"."+namespace
//	if cachedResponse, hit := f.Cache.Get(funcNameKey); hit &&
//		cachedResponse.AvailableReplicas > 0 {
//		return FunctionScaleResult{
//			Error:     nil,
//			Available: true,
//			Found:     true,
//			Duration:  time.Since(start),
//		}
//	}
//
//	if val, exists := f.CacheUpdateFlag.GetFlag(funcNameKey); exists {
//		if val == false { // nobody got in
//			f.CacheUpdateFlag.SetFlag(funcNameKey,true) //  then get in and lock the door (flag)
//			//cacheUpdateFlag[funcNameKey] = true
//			queryResponse, err := GetReplicas(functionName, namespace, user)  // external.go implement
//			if err != nil {
//				f.CacheUpdateFlag.SetFlag(funcNameKey,false)
//				return FunctionScaleResult {
//					Error:     err,
//					Available: false,
//					Found:     false,
//					Duration:  time.Since(start),
//				}
//			}
//
//			f.Cache.Set(funcNameKey, queryResponse)
//
//			if queryResponse.AvailableReplicas == 0 {
//				minReplicas := uint64(1)
//				if queryResponse.MinReplicas > 0 {
//					minReplicas = queryResponse.MinReplicas
//				}
//
//				scaleResultErr := backoff(func(attempt int) error {
//					if queryResponse.Replicas > 0 {
//						return nil
//					}
//					log.Printf("[Scale %d] function=%s 0 => %d requested", attempt, functionName, minReplicas)
//					setScaleErr := SetReplicas(functionName, namespace, minReplicas,user)
//					if setScaleErr != nil {
//						return fmt.Errorf("unable to scale function [%s], err: %s", functionName, setScaleErr)
//					}
//
//					return nil
//
//				}, int(f.Config.SetScaleRetries), f.Config.FunctionPollInterval)
//
//				if scaleResultErr != nil {
//					f.CacheUpdateFlag.SetFlag(funcNameKey, false)
//					return FunctionScaleResult{
//						Error:     scaleResultErr,
//						Available: false,
//						Found:     true,
//						Duration:  time.Since(start),
//					}
//				}
//
//				for i := 0; i < int(f.Config.MaxPollCount); i++ {
//					queryResponse, err := GetReplicas(functionName, namespace,user)
//					if err == nil {
//						f.Cache.Set(funcNameKey, queryResponse)
//					}
//					totalTime := time.Since(start)
//
//					if err != nil {
//						f.CacheUpdateFlag.SetFlag(funcNameKey, false)
//						return FunctionScaleResult{
//							Error:     err,
//							Available: false,
//							Found:     true,
//							Duration:  totalTime,
//						}
//					}
//
//					if queryResponse.AvailableReplicas > 0 {
//						log.Printf("user %d [Scale] function=%s 0 => %d successful - %fs", user,
//							functionName, queryResponse.AvailableReplicas, totalTime.Seconds())
//						f.CacheUpdateFlag.SetFlag(funcNameKey, false)
//						return FunctionScaleResult {
//							Error:     nil,
//							Available: true,
//							Found:     true,
//							Duration:  totalTime,
//						}
//					}
//					time.Sleep(f.Config.FunctionPollInterval)
//				}
//			} else {
//				f.CacheUpdateFlag.SetFlag(funcNameKey, false) // get out and unlock the door
//			}
//		} else {
//			count := 0
//			for{
//				if count > 10 {
//					break
//				} else {
//					count++
//				}
//				time.Sleep(time.Millisecond*100)
//				log.Printf("user %d waiting the door count=%d \n",user, count)
//				if  doorIsClosed, _ := f.CacheUpdateFlag.GetFlag(funcNameKey); doorIsClosed == false { // wait util someone unlock the door
//					log.Printf("user %d door opens count=%d\n",user, count)
//					break
//				}
//			}
//		}
//	} else {
//		f.CacheUpdateFlag.SetFlag(funcNameKey,false)
//		//cacheUpdateFlag[funcNameKey] = false
//		//  then get in and lock the door (flag)
//		if doorIsClosed, _ := f.CacheUpdateFlag.GetFlag(funcNameKey); doorIsClosed == false {
//			f.CacheUpdateFlag.SetFlag(funcNameKey,true)
//			//cacheUpdateFlag[funcNameKey] = true
//			queryResponse, err := GetReplicas(functionName, namespace, user)  // external.go implement
//			if err != nil {
//				f.CacheUpdateFlag.SetFlag(funcNameKey,false)
//				return FunctionScaleResult{
//					Error:     err,
//					Available: false,
//					Found:     false,
//					Duration:  time.Since(start),
//				}
//			}
//
//			f.Cache.Set(funcNameKey, queryResponse)
//
//			if queryResponse.AvailableReplicas == 0 {
//				minReplicas := uint64(1)
//				if queryResponse.MinReplicas > 0 {
//					minReplicas = queryResponse.MinReplicas
//				}
//
//				scaleResult := backoff(func(attempt int) error {
//					if queryResponse.Replicas > 0 { // expected is not 0
//						return nil
//					}
//
//					log.Printf("user %d [Scale %d] function=%s 0 => %d requested",user, attempt, functionName, minReplicas)
//					setScaleErr := SetReplicas(functionName, namespace, minReplicas, user)
//					if setScaleErr != nil {
//						return fmt.Errorf("user %d unable to scale function [%s], err: %s", user, functionName, setScaleErr)
//					}
//					return nil
//
//				}, int(f.Config.SetScaleRetries), f.Config.FunctionPollInterval)
//
//				if scaleResult != nil {
//					f.CacheUpdateFlag.SetFlag(funcNameKey,false)
//					return FunctionScaleResult{
//						Error:     scaleResult,
//						Available: false,
//						Found:     true,
//						Duration:  time.Since(start),
//					}
//				}
//
//				for i := 0; i < int(f.Config.MaxPollCount); i++ {
//					queryResponse, err := GetReplicas(functionName, namespace, user)
//					if err == nil {
//						f.Cache.Set(funcNameKey, queryResponse)
//					}
//					totalTime := time.Since(start)
//
//					if err != nil {
//						f.CacheUpdateFlag.SetFlag(funcNameKey,false)
//						return FunctionScaleResult{
//							Error:     err,
//							Available: false,
//							Found:     true,
//							Duration:  totalTime,
//						}
//					}
//
//					if queryResponse.AvailableReplicas > 0 {
//						log.Printf("user %d [Scale] function=%s 0 => %d successful - %fs", user,
//							functionName, queryResponse.AvailableReplicas, totalTime.Seconds())
//						f.CacheUpdateFlag.SetFlag(funcNameKey,false)
//						return FunctionScaleResult{
//							Error:     nil,
//							Available: true,
//							Found:     true,
//							Duration:  totalTime,
//						}
//					}
//					time.Sleep(f.Config.FunctionPollInterval)
//				}
//			} else {
//				f.CacheUpdateFlag.SetFlag(funcNameKey,false) // get out and unlock the door
//			}
//
//		} else {
//			count := 0
//			for{
//				if count > 10 {
//					break
//				} else {
//					count++
//				}
//				time.Sleep(time.Millisecond*100)
//				log.Printf("user %d waiting the door count=%d \n",user, count)
//				if doorIsClosed, _ = f.CacheUpdateFlag.GetFlag(funcNameKey); doorIsClosed == false {// wait util someone unlock the door
//					log.Printf("user %d door opens count=%d\n",user, count)
//					break
//				}
//			}
//		}
//	}
//
//	return FunctionScaleResult{
//		Error:     nil,
//		Available: true,
//		Found:     true,
//		Duration:  time.Since(start),
//	}
//}
//type routine func(attempt int) error
//func backoff(r routine, attempts int, interval time.Duration) error {
//	var err error
//
//	for i := 0; i < attempts; i++ {
//		res := r(i)
//		if res != nil {
//			err = res
//
//			log.Printf("Attempt: %d, had error: %s\n", i, res)
//		} else {
//			err = nil
//			break
//		}
//		time.Sleep(interval)
//	}
//	return err
//}
//func GetReplicas(serviceName, serviceNamespace string, user int) (ServiceQueryResponse, error) {
//	start := time.Now()
//
//	var err error
//	var emptyServiceQueryResponse ServiceQueryResponse
//	log.Printf("user %d %ssystem/function/%s?namespace=%s \n",user, "http://", serviceName, serviceNamespace)
//	minReplicas := uint64(1)
//	maxReplicas := uint64(20)
//	scalingFactor := uint64(20)
//	availableReplicas := rand.Intn(3)
//	replicas := availableReplicas
//	time.Sleep(time.Millisecond*200) // http request
//	if 200 == http.StatusOK {
//		log.Printf("user %d GetReplicas [%s.%s] =%d took: %fs",user, serviceName, serviceNamespace, availableReplicas, time.Since(start).Seconds())
//	} else {
//		log.Printf("user %d GetReplicas [%s.%s] =%d took: %fs, code: %d\n",user, serviceName, serviceNamespace, availableReplicas, time.Since(start).Seconds(), 202)
//		return emptyServiceQueryResponse, fmt.Errorf("user %d server returned non-200 status code (%d) for function, %s", user,202, serviceName)
//	}
//
//	//log.Printf("GetReplicas [%s.%s] took: %fs", serviceName, serviceNamespace, time.Since(start).Seconds())
//
//	return ServiceQueryResponse{
//		Replicas:          uint64(replicas),
//		MaxReplicas:       maxReplicas,
//		MinReplicas:       minReplicas,
//		ScalingFactor:     scalingFactor,
//		AvailableReplicas: uint64(availableReplicas),
//	}, err
//}
//type ScaleServiceRequest struct {
//	ServiceName      string `json:"serviceName"`
//	ServiceNamespace string `json:"serviceNamespace"`
//	Replicas         uint64 `json:"replicas"`
//}
//// SetReplicas update the replica count
//func SetReplicas(serviceName, serviceNamespace string, count uint64, user int) error {
//	var err error
//
//	log.Printf("user %d %ssystem/scale-function/%s?namespace=%s&replicas=%d \n",user, "http://", serviceName, serviceNamespace, count)
//
//	start := time.Now()
//	time.Sleep(time.Millisecond*150)
//	log.Printf("user %d SetReplicas [%s.%s] took: %fs", user, serviceName, serviceNamespace, time.Since(start).Seconds())
//
//	return err
//}
//
//func testDelete(lookupNamespace string, functionName string, clientset *kubernetes.Clientset){
//	// build the label of the pods which will be deleted
//	labelPod := labels.SelectorFromSet(map[string]string{"faas_function": functionName})
//	listPodOptions := metav1.ListOptions{
//		LabelSelector: labelPod.String(),
//	}
//	// This makes sure we don't delete non-labeled deployments
//	podList, findPodsErr := clientset.CoreV1().Pods(lookupNamespace).List(listPodOptions)
//	if findPodsErr != nil {
//		if errors.IsNotFound(findPodsErr) {
//			log.Println(http.StatusNotFound)
//		} else {
//			log.Println(http.StatusInternalServerError)
//		}
//		log.Println([]byte(findPodsErr.Error()))
//		return
//	}
//
//	if podList != nil && len(podList.Items) > 0 {
//		log.Printf("delete: find existing %d pods for function: %s \n", len(podList.Items), functionName)
//		err := deleteFunctionPod(lookupNamespace, listPodOptions, clientset)
//		if err != nil {
//			log.Println(err)
//			return
//		}
//	} else {
//		log.Println(http.StatusBadRequest)
//		log.Println([]byte("delete: can't find existing pods for function: " + functionName))
//	}
//
//	srvErr := deleteFunctionService(lookupNamespace, functionName, listPodOptions, clientset)
//	if srvErr != nil {
//		log.Println(srvErr)
//		return
//	} else {
//		log.Printf("delete: find existing service for function: %s \n", functionName)
//	}
//
//	repository.DeleteFunc(functionName)
//}
//func testReplicas(lookupNamespace string, functionName string, clientset *kubernetes.Clientset){
//	funcDeployStatus := repository.GetFunc(functionName)
//	if funcDeployStatus == nil {
//		fmt.Println(http.StatusInternalServerError)
//		fmt.Println("replicas: Unable to lookup function deployment " + functionName)
//		return
//	}
//	resourceLimits := &bootTypes.FunctionResources {
//		Memory:     funcDeployStatus.FuncResources.Memory,
//		CPU:        funcDeployStatus.FuncResources.CPU,
//		GPU:        funcDeployStatus.FuncResources.GPU,
//		GPU_Memory: funcDeployStatus.FuncResources.GPU_Memory,
//	}
//
//	differ := funcDeployStatus.ExpectedReplicas - funcDeployStatus.AvailReplicas
//	if differ > 0 {
//		scaleUpFunc(funcDeployStatus, lookupNamespace, resourceLimits, differ, clientset)
//	} else if differ < 0 {
//		scaleDownFunc(funcDeployStatus, lookupNamespace, -differ, clientset)
//	} else {
//		fmt.Println("-----------------expectedReplicas=availReplicas do nothing-----------------------")
//		// expectedReplicas=availReplicas do nothing
//	}
//}
//
//
//
//func testReaderList(functionNamespace string, clientset *kubernetes.Clientset){
//	var functions []ptypes.FunctionStatus // init = nil
//	var function *ptypes.FunctionStatus // init = nil
//
//	// search service firstly
//	listOpts := metav1.ListOptions{}
//	srvs, srvErr := clientset.CoreV1().Services(functionNamespace).List(listOpts)
//	if srvErr != nil {
//		log.Println(srvErr.Error())
//	}
//	for _, srvItem := range srvs.Items {
//		fmt.Println(srvItem.Name)
//		// search pod secondly
//		function = k8s.CreateFunctionPodStatus(srvItem.Name) // then read repository to get the pod information
//		if function == nil {
//			log.Printf("reader: function'pod %s not found \n", srvItem.Name)
//		} else {
//			//log.Printf("reader: create a func status for function %s from repository, ExpectedReplicas= %d, AvailReplicas= %d \n",srvItem.Name, function.Replicas, function.AvailableReplicas)
//			functions = append(functions, *function)
//		}
//	}
//}
//
//
//func testReader(functionNamespace string, functionName string, clientset *kubernetes.Clientset){
//	functionName="sleep"
//
//	// search service firstly
//	srvs, srvErr := clientset.CoreV1().Services(functionNamespace).Get(functionName,metav1.GetOptions{})
//	if srvErr != nil {
//		panic(srvErr.Error())
//	}
//	fmt.Println("===================================")
//	if srvs.Name == functionName { //find this function's service
//		// search pod secondly
//		function := k8s.CreateFunctionPodStatus(functionName) // then read repository to get the pod information
//		// result check
//		if function == nil {
//			log.Printf("reader: function's pod %s not found \n", functionName)
//		} else {
//			log.Printf("reader: create a func status for function %s from repository, ExpectedReplicas= %d, AvailReplicas= %d \n",functionName, function.Replicas, function.AvailableReplicas)
//		}
//	} else {
//		log.Printf("reader: function's service %s not found \n", functionName)
//	}
//
//}
//
//
//func testDeploy(clientset *kubernetes.Clientset, factory k8s.FunctionFactory) {
//	serviceName := "sleep"
//	var constaints []string
//	initialReplicas := int32p(2)
//	repository.UpdateFuncMinReplicas(serviceName, *initialReplicas)
//	namespace := "test"
//	resourceRequests := &bootTypes.FunctionResources {
//		Memory:     "100Mi",
//		CPU:        "2000m",
//		GPU:        "0",
//		GPU_Memory: "0.4",
//	}
//	lable := map[string]string{}
//	lable["com.openfaas.cpu.bind"]="10,12"
//	request := bootTypes.FunctionDeployment {
//		Service:                serviceName,
//		Image:                  "sleep:latest",
//		Network:                "",
//		EnvProcess:             "python index.py",
//		EnvVars:                nil,
//		RegistryAuth:           "",
//		Constraints:            constaints,
//		Secrets:                []string{},
//		Labels:                 &lable,
//		Annotations:            nil,
//		Limits:                 nil,
//		Requests:               resourceRequests,
//		ReadOnlyRootFilesystem: false,
//		Namespace:              "",
//	}
//
//	secrets := k8s.NewSecretsClient(factory.Client)
//	existingSecrets, werr := secrets.GetSecrets(namespace, request.Secrets)
//	if werr != nil {
//		wrappedErr := fmt.Errorf("deploy: unable to fetch secrets: %s \n", werr.Error())
//		log.Println(wrappedErr.Error())
//		return
//	}
//
//	repository.UpdateFuncRequestResources(serviceName, request.Requests)
//	repository.UpdateFuncConstrains(serviceName, constaints)
//	repository.UpdateFuncExpectedReplicas(serviceName, *initialReplicas)
//
//	var latestPodSepc *corev1.Pod
//	for i := *int32p(0); i < *initialReplicas; i++ {
//		pod, nodeGpuAlloc, specErr := makePodSpecTest(serviceName, constaints, request, factory, existingSecrets)
//		if specErr != nil {
//			wrappedErr := fmt.Errorf("deploy: failed make Pod spec for replica = %d: %s \n", i, specErr.Error())
//			log.Println(wrappedErr)
//			//http.Error(w, wrappedErr.Error(), http.StatusBadRequest)
//			return
//		}
//		_, err := clientset.CoreV1().Pods(namespace).Create(pod)
//		if err != nil {
//			wrappedErr := fmt.Errorf("unable create Pod for replicas %d: %s \n", i, err.Error())
//			log.Println(wrappedErr)
//			//http.Error(w, wrappedErr.Error(), http.StatusInternalServerError)
//			return
//		}
//		fmt.Println("created-----------------")
//		// search pod secondly
//		//listOpts.LabelSelector = "faas_function=" + serviceName
//		var ii =0
//		go func() {
//			for {
//				ii++
//				if ii > 30 {
//					break
//				}
//
//				pods, podErr := clientset.CoreV1().Pods("test").Get(pod.Name, metav1.GetOptions{})
//				if podErr != nil {
//					log.Println(podErr.Error())
//				}
//				if pods.Status.PodIP == "" {
//					fmt.Println("sleeping in go func-------------")
//				} else {
//					fmt.Println(pods.Status.PodIP)
//					break
//				}
//				time.Sleep(time.Second * 1)
//			}
//		}()
//
//		latestPodSepc = pod //update pointer
//		fmt.Println("latestPodSepc-----------------")
//		repository.UpdateFuncAvailReplicas(serviceName, i+1)
//
//		funcPodConfig := gpuTypes.FuncPodConfig{
//			FuncPodName:          "",
//			BatchSize:            0,
//			CpuThreads:           0,
//			GpuCorePercent:       0,
//			ExecutionTime:        0,
//			ReqPerSecondMax:         0,
//			FuncPodIp:            "",
//			NodeGpuCpuAllocation: nodeGpuAlloc,
//		}
//		repository.UpdateFuncPodConfig(serviceName, &funcPodConfig)
//		log.Printf("deploy: Deployment (pods with replicas = %d) created: %s.%s \n", i+1, serviceName, namespace)
//	}
//	repository.UpdateFuncSpec(serviceName, latestPodSepc, nil)
//	// after that, deploy the service to find the pods with special label
//	serviceSpec := makeServiceSpecTest(serviceName, request, factory)
//	_, err := clientset.CoreV1().Services(namespace).Create(serviceSpec)
//	if err != nil {
//		wrappedErr := fmt.Errorf("deploy: failed create Service: %s \n", err.Error())
//		log.Println(wrappedErr)
//		//http.Error(w, wrappedErr.Error(), http.StatusBadRequest)
//	}
//	repository.UpdateFuncSpec(serviceName, nil, serviceSpec) //update functionSpec map
//	log.Printf("deploy: service created: %s.%s \n", serviceName, namespace)
//
//	return
//}
//
//func int32p(i int32) *int32 {
//	return &i
//}
//
//
//
//func makePodSpecTest(serviceName string, constaints []string, request bootTypes.FunctionDeployment, factory k8s.FunctionFactory, secret map[string]*corev1.Secret) (*corev1.Pod, *gpuTypes.NodeGpuCpuAllocation, error) {
//	envVars := buildEnvVars(&request)
//
//	labels := map[string]string{
//		"faas_function": serviceName,
//	}
//
//	// GPU card selection start
//	nodeSelector := map[string]string{} // init=map{}
//	var nodeGpuAlloc *gpuTypes.NodeGpuCpuAllocation // init=nil
//	if constaints != nil && len(constaints) > 0 {
//		nodeSelector = createSelector(constaints) // user's defination first
//	} else {
//		nodeGpuAlloc = scheduler.FindGpuDeployNode(request.Requests,request.Constraints) // only for GPU and GPU_Memory
//		if nodeGpuAlloc == nil {
//			log.Println("deploy: no node for select")
//			return nil, nil, nil
//		}
//		// build the node selector
//		nodeLabelStrList := strings.Split(repository.GetClusterCapConfig().
//			ClusterCapacity[nodeGpuAlloc.NodeTh].NodeLabel, "=")
//		nodeSelector[nodeLabelStrList[0]] = nodeLabelStrList[1]
//
//		envVars = append(envVars, corev1.EnvVar{
//			Name:  "CUDA_VISIBLE_DEVICES",
//			Value: strconv.Itoa(nodeGpuAlloc.CudaDeviceTh),
//		})
//		envVars = append(envVars, corev1.EnvVar{
//			Name:  "GPU_MEM_FRACTION",
//			Value: request.Requests.GPU_Memory,
//		})
//		log.Println("deploy: GPU node selection = ", envVars[0])
//	}
//	// GPU card selection end
//
//	resources, resourceErr := createResources(request) // only for CPU and memory
//
//	if resourceErr != nil {
//		return nil, nil, resourceErr
//	}
//
//	var imagePullPolicy corev1.PullPolicy
//	switch "Never" {
//	case "Never":
//		imagePullPolicy = corev1.PullNever
//	case "IfNotPresent":
//		imagePullPolicy = corev1.PullIfNotPresent
//	default:
//		imagePullPolicy = corev1.PullAlways
//	}
//
//	annotations := buildAnnotations(request)
//
//	var serviceAccount string
//
//	if request.Annotations != nil {
//		annotations := *request.Annotations
//		if val, ok := annotations["com.openfaas.serviceaccount"]; ok && len(val) > 0 {
//			serviceAccount = val
//		}
//	}
//
//	probes, err := factory.MakeProbes(request)
//	if err != nil {
//		return nil, nil, err
//	}
//
//	pod := &corev1.Pod{
//		TypeMeta: metav1.TypeMeta{
//			Kind:       "Pod",
//			APIVersion: "v1",
//		},
//		ObjectMeta: metav1.ObjectMeta{
//			Name:        request.Service + "-n"+strconv.Itoa(nodeGpuAlloc.NodeTh)+"-g"+strconv.Itoa(nodeGpuAlloc.CudaDeviceTh)+"pod-" + tools.RandomText(10),
//			Annotations: annotations, //prometheus.io.scrape: false
//			Labels: labels,
//			//labels: com.openfaas.scale.max=15 com.openfaas.scale.min=1 com.openfaas.scale.zero=true
//			//faas_function=mnist-test uid=44642818
//		},
//		Spec: corev1.PodSpec{
//			NodeSelector: nodeSelector,
//			Containers: []corev1.Container{
//				{
//					Name:  request.Service + "-con",
//					Image: request.Image,
//					Ports: []corev1.ContainerPort{
//						{
//							ContainerPort: factory.Config.RuntimeHTTPPort,
//							Protocol: corev1.ProtocolTCP},
//					},
//					Env:             envVars,
//					Resources:       *resources,
//					ImagePullPolicy: imagePullPolicy,
//					LivenessProbe:   probes.Liveness,
//					ReadinessProbe:  probes.Readiness,
//					SecurityContext: &corev1.SecurityContext{
//						ReadOnlyRootFilesystem: &request.ReadOnlyRootFilesystem,
//					},
//				},
//			},
//			ServiceAccountName: serviceAccount,
//			RestartPolicy:      corev1.RestartPolicyAlways,
//			DNSPolicy:          corev1.DNSClusterFirst,
//		},
//	}
//
//	factory.ConfigureReadOnlyRootFilesystem(request, pod)
//	factory.ConfigureContainerUserID(pod)
//
//	if err := factory.ConfigureSecrets(request, pod, secret); err != nil {
//		return nil, nil, err
//	}
//
//	return pod, nodeGpuAlloc, nil
//}
//
//
//func makeServiceSpecTest(serviceName string, request bootTypes.FunctionDeployment,factory k8s.FunctionFactory) *corev1.Service {
//
//	service := &corev1.Service{
//		TypeMeta: metav1.TypeMeta{
//			Kind:       "Service",
//			APIVersion: "v1",
//		},
//		ObjectMeta: metav1.ObjectMeta {
//			Name:        request.Service,
//			Annotations: buildAnnotations(request),
//		},
//		Spec: corev1.ServiceSpec{
//			Type: corev1.ServiceTypeClusterIP,
//			Selector: map[string]string {
//				"faas_function": request.Service,
//			},
//			Ports: []corev1.ServicePort{
//				{
//					Name:     "http",
//					Protocol: corev1.ProtocolTCP,
//					Port:     factory.Config.RuntimeHTTPPort,
//					TargetPort: intstr.IntOrString{
//						Type:   intstr.Int,
//						IntVal: factory.Config.RuntimeHTTPPort,
//					},
//				},
//			},
//		},
//	}
//
//	return service
//}
//
//func createSelector(constraints []string) map[string]string {
//	selector := make(map[string]string)
//
//	if len(constraints) > 0 {
//		for _, constraint := range constraints {
//			parts := strings.Split(constraint, "=")
//			if len(parts) == 2 {
//				selector[parts[0]] = parts[1]
//			}
//		}
//	}
//
//	return selector
//}
//
//func createResources(request bootTypes.FunctionDeployment) (*corev1.ResourceRequirements, error) {
//	resources := &corev1.ResourceRequirements{
//		Limits:   corev1.ResourceList{},
//		Requests: corev1.ResourceList{},
//	}
//
//	// Set Memory limits
//	if request.Limits != nil && len(request.Limits.Memory) > 0 {
//		qty, err := resource.ParseQuantity(request.Limits.Memory)
//		if err != nil {
//			return resources, err
//		}
//		resources.Limits[corev1.ResourceMemory] = qty
//	}
//
//	if request.Requests != nil && len(request.Requests.Memory) > 0 {
//		qty, err := resource.ParseQuantity(request.Requests.Memory)
//		if err != nil {
//			return resources, err
//		}
//		resources.Requests[corev1.ResourceMemory] = qty
//	}
//
//	// Set CPU limits
//	if request.Limits != nil && len(request.Limits.CPU) > 0 {
//		qty, err := resource.ParseQuantity(request.Limits.CPU)
//		if err != nil {
//			return resources, err
//		}
//		resources.Limits[corev1.ResourceCPU] = qty
//	}
//
//	if request.Requests != nil && len(request.Requests.CPU) > 0 {
//		qty, err := resource.ParseQuantity(request.Requests.CPU)
//		if err != nil {
//			return resources, err
//		}
//		resources.Requests[corev1.ResourceCPU] = qty
//	}
//
//	return resources, nil
//}
//
//func buildAnnotations(request bootTypes.FunctionDeployment) map[string]string {
//	var annotations map[string]string
//	if request.Annotations != nil {
//		annotations = *request.Annotations
//	} else {
//		annotations = map[string]string{}
//	}
//
//	annotations["prometheus.io.scrape"] = "false"
//	return annotations
//}
//func buildEnvVars(request *bootTypes.FunctionDeployment) []corev1.EnvVar {
//	var envVars []corev1.EnvVar
//
//	if len(request.EnvProcess) > 0 {
//		envVars = append(envVars, corev1.EnvVar{
//			Name:  k8s.EnvProcessName,
//			Value: request.EnvProcess,
//		})
//	}
//
//	for k, v := range request.EnvVars {
//		envVars = append(envVars, corev1.EnvVar{
//			Name:  k,
//			Value: v,
//		})
//	}
//
//
//	sort.SliceStable(envVars, func(i, j int) bool {
//		return strings.Compare(envVars[i].Name, envVars[j].Name) == -1
//	})
//
//	return envVars
//}
//
//func scaleUpFunc(funcDeployStatus *gpuTypes.FuncDeployStatus, namespace string, resourceLimits *bootTypes.FunctionResources, differ int32, clientset *kubernetes.Clientset){
//	nodeSelector := map[string]string{} // init=map{}
//	var nodeGpuAlloc *gpuTypes.NodeGpuCpuAllocation // init=nil
//
//	// there is need to decide the new pod's name and deploy node
//	for i := int32(0); i < differ; i++ {
//		if funcDeployStatus.FuncPlaceConstraints != nil && len(funcDeployStatus.FuncPlaceConstraints) > 0 {
//			nodeSelector = createSelector(funcDeployStatus.FuncPlaceConstraints) // user's defination first
//		} else {
//			nodeGpuAlloc = scheduler.FindGpuDeployNode(resourceLimits,funcDeployStatus.FuncPlaceConstraints) // only for GPU and GPU_Memory
//			if nodeGpuAlloc == nil {
//				log.Println("replicas: no node for select")
//				return
//			}
//			// build the node selector
//			nodeLabelStrList := strings.Split(repository.GetClusterCapConfig().ClusterCapacity[nodeGpuAlloc.NodeTh].NodeLabel, "=")
//			nodeSelector[nodeLabelStrList[0]] = nodeLabelStrList[1]
//			// build the cuda device env str
//			cudaDeviceIndexEnvStr := strconv.Itoa(nodeGpuAlloc.CudaDeviceTh)
//
//			envItemSize := len(funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env)
//			for i := 0; i < envItemSize; i++ {
//				if funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env[i].Name == "CUDA_VISIBLE_DEVICES" {
//					funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env[i].Value = cudaDeviceIndexEnvStr
//					break
//				}
//			}
//			for i := 0; i < envItemSize; i++ {
//				if funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env[i].Name == "GPU_MEM_FRACTION" {
//					funcDeployStatus.FuncSpec.Pod.Spec.Containers[0].Env[i].Value = funcDeployStatus.FuncResources.GPU_Memory
//					break
//				}
//			}
//		}
//		funcDeployStatus.FuncSpec.Pod.Name = funcDeployStatus.FunctionName + "-pod-" + tools.RandomText(10)
//		funcDeployStatus.FuncSpec.Pod.Spec.NodeSelector = nodeSelector
//		_, err := clientset.CoreV1().Pods(namespace).Create(funcDeployStatus.FuncSpec.Pod)
//		if err != nil {
//			wrappedErr := fmt.Errorf("replicas: scaleup function %s 's Pod for differ %d error: %s \n", funcDeployStatus.FunctionName, i+1, err.Error())
//			log.Println(wrappedErr)
//			return
//		}
//		repository.UpdateFuncAvailReplicas(funcDeployStatus.FunctionName, funcDeployStatus.AvailReplicas+1)
//		funcPodConfig := gpuTypes.FuncPodConfig {
//			FuncPodName:          "",
//			BatchSize:            0,
//			CpuThreads:           0,
//			GpuCorePercent:       0,
//			ExecutionTime:        0,
//			ReqPerSecondMax:         0,
//			FuncPodIp:            "",
//			NodeGpuCpuAllocation: nodeGpuAlloc,
//		}
//		repository.UpdateFuncPodConfig(funcDeployStatus.FunctionName, &funcPodConfig)
//		log.Printf("replicas: scaleup function %s 's Pod for differ %d successfully \n", funcDeployStatus.FunctionName, i+1)
//	}
//}
//func scaleDownFunc(funcDeployStatus *gpuTypes.FuncDeployStatus, namespace string, differ int32, clientset *kubernetes.Clientset){
//
//	if funcDeployStatus.AvailReplicas < differ {
//		log.Printf("replicas: function %s does not has enough instances %d for differ %d \n", funcDeployStatus.FunctionName, funcDeployStatus.AvailReplicas, differ)
//		return
//	}
//	foregroundPolicy := metav1.DeletePropagationForeground
//	opts := &metav1.DeleteOptions{PropagationPolicy: &foregroundPolicy}
//
//	for i := int32(0); i < differ; i++ {
//		podName := scheduler.FindGpuDeletePod(funcDeployStatus)
//		err := clientset.CoreV1().Pods(namespace).Delete(podName, opts)
//		if err != nil {
//			log.Printf("replicas: function %s deleted pod %s error \n", funcDeployStatus.FunctionName, podName)
//		}
//		log.Printf("replicas: function %s deleted pod %s successfully \n", funcDeployStatus.FunctionName, podName)
//
//		repository.UpdateFuncAvailReplicas(funcDeployStatus.FunctionName, funcDeployStatus.AvailReplicas-1)
//		repository.DeleteFuncPodLocation(funcDeployStatus.FunctionName, podName)
//	}
//
//}
//
//func deleteFunctionPod(functionNamespace string, listPodOptions metav1.ListOptions, clientset *kubernetes.Clientset) error {
//	foregroundPolicy := metav1.DeletePropagationForeground
//	opts := &metav1.DeleteOptions{PropagationPolicy: &foregroundPolicy}
//
//	if  deletePodsErr := clientset.CoreV1().Pods(functionNamespace).DeleteCollection(opts, listPodOptions); deletePodsErr != nil {
//		if errors.IsNotFound(deletePodsErr) {
//			log.Println(http.StatusNotFound)
//		} else {
//			log.Println(http.StatusInternalServerError)
//		}
//		log.Println([]byte(deletePodsErr.Error()))
//		return fmt.Errorf("delete: delete function %s pods error \n", listPodOptions.LabelSelector)
//	}
//	//log.Printf("delete: delete function %s pods successfully \n", listPodOptions.LabelSelector)
//
//	return nil
//}
//func deleteFunctionService(functionNamespace string, functionName string, listPodOptions metav1.ListOptions, clientset *kubernetes.Clientset) error {
//	foregroundPolicy := metav1.DeletePropagationForeground
//	opts := &metav1.DeleteOptions{PropagationPolicy: &foregroundPolicy}
//
//	if svcErr := clientset.CoreV1().Services(functionNamespace).Delete(functionName, opts); svcErr != nil {
//		if errors.IsNotFound(svcErr) {
//			log.Println(http.StatusNotFound)
//		} else {
//			log.Println(http.StatusInternalServerError)
//		}
//
//		log.Println([]byte(svcErr.Error()))
//		return fmt.Errorf("delete: delete function %s service error \n", listPodOptions.LabelSelector)
//	}
//	log.Printf("delete: delete function %s service successfully \n", listPodOptions.LabelSelector)
//
//	return nil
//}



