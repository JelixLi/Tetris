// Copyright (c) Alex Ellis 2017. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

package commands

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strings"

	"github.com/docker/docker-credential-helpers/client"
	homedir "github.com/mitchellh/go-homedir"
	"github.com/openfaas/faas-cli/builder"
	"github.com/openfaas/faas-cli/proxy"
	"github.com/openfaas/faas-cli/schema"
	"github.com/openfaas/faas-cli/stack"
	"github.com/spf13/cobra"
	yaml "gopkg.in/yaml.v2"
)

var (
	// readTemplate controls whether we should read the function's template when deploying.
	readTemplate bool
)

// DeployFlags holds flags that are to be added to commands.
type DeployFlags struct {
	envvarOpts             []string
	replace                bool
	update                 bool
	readOnlyRootFilesystem bool
	constraints            []string
	secrets                []string
	labelOpts              []string
	annotationOpts         []string
	sendRegistryAuth       bool
}

var deployFlags DeployFlags

func init() {
	// Setup flags that are used by multiple commands (variables defined in faas.go)
	deployCmd.Flags().StringVar(&fprocess, "fprocess", "", "fprocess value to be run as a serverless function by the watchdog")
	deployCmd.Flags().StringVarP(&gateway, "gateway", "g", defaultGateway, "Gateway URL starting with http(s)://")
	deployCmd.Flags().StringVar(&handler, "handler", "", "Directory with handler for function, e.g. handler.js")
	deployCmd.Flags().StringVar(&image, "image", "", "Docker image name to build")
	deployCmd.Flags().StringVar(&qpsPerInstance, "qpsPerInstance", "", "QPS per function instance can support")
	deployCmd.Flags().StringVar(&language, "lang", "", "Programming language template")
	deployCmd.Flags().StringVar(&functionName, "name", "", "Name of the deployed function")
	deployCmd.Flags().StringVar(&network, "network", defaultNetwork, "Name of the network")
	deployCmd.Flags().StringVarP(&functionNamespace, "namespace", "n", "", "Namespace of the function")

	// Setup flags that are used only by this command (variables defined above)
	deployCmd.Flags().StringArrayVarP(&deployFlags.envvarOpts, "env", "e", []string{}, "Set one or more environment variables (ENVVAR=VALUE)")

	deployCmd.Flags().StringArrayVarP(&deployFlags.labelOpts, "label", "l", []string{}, "Set one or more label (LABEL=VALUE)")

	deployCmd.Flags().StringArrayVarP(&deployFlags.annotationOpts, "annotation", "", []string{}, "Set one or more annotation (ANNOTATION=VALUE)")

	deployCmd.Flags().BoolVar(&deployFlags.replace, "replace", false, "Remove and re-create existing function(s)")
	deployCmd.Flags().BoolVar(&deployFlags.update, "update", false, "Perform rolling update on existing function(s)")

	deployCmd.Flags().StringArrayVar(&deployFlags.constraints, "constraint", []string{}, "Apply a constraint to the function")
	deployCmd.Flags().StringArrayVar(&deployFlags.secrets, "secret", []string{}, "Give the function access to a secure secret")
	deployCmd.Flags().BoolVar(&deployFlags.readOnlyRootFilesystem, "readonly", false, "Force the root container filesystem to be read only")

	deployCmd.Flags().BoolVarP(&deployFlags.sendRegistryAuth, "send-registry-auth", "a", false, "send registryAuth from Docker credentials manager with the request")
	deployCmd.Flags().Var(&tagFormat, "tag", "Override latest tag on function Docker image, accepts 'latest', 'sha', 'branch', or 'describe'")

	deployCmd.Flags().BoolVar(&tlsInsecure, "tls-no-verify", false, "Disable TLS validation")
	deployCmd.Flags().BoolVar(&envsubst, "envsubst", true, "Substitute environment variables in stack.yml file")
	deployCmd.Flags().StringVarP(&token, "token", "k", "", "Pass a JWT token to use instead of basic auth")
	// Set bash-completion.
	_ = deployCmd.Flags().SetAnnotation("handler", cobra.BashCompSubdirsInDir, []string{})
	deployCmd.Flags().BoolVar(&readTemplate, "read-template", true, "Read the function's template")

	faasCmd.AddCommand(deployCmd)
}

// deployCmd handles deploying OpenFaaS function containers
var deployCmd = &cobra.Command{
	Use: `deploy -f YAML_FILE [--replace=false]
  faas-cli deploy --image IMAGE_NAME
                  --name FUNCTION_NAME
                  [--lang <ruby|python|node|csharp>]
                  [--gateway GATEWAY_URL]
                  [--qpsPerInstance QPS]
                  [--network NETWORK_NAME]
                  [--handler HANDLER_DIR]
                  [--fprocess PROCESS]
                  [--env ENVVAR=VALUE ...]
				  [--label LABEL=VALUE ...]
				  [--annotation ANNOTATION=VALUE ...]
				  [--replace=false]
				  [--update=false]
                  [--constraint PLACEMENT_CONSTRAINT ...]
                  [--regex "REGEX"]
                  [--filter "WILDCARD"]
				  [--secret "SECRET_NAME"]
				  [--tag <sha|branch|describe>]
				  [--readonly=false]
				  [--tls-no-verify]`,

	Short: "Deploy OpenFaaS functions",
	Long: `Deploys OpenFaaS function containers either via the supplied YAML config using
the "--yaml" flag (which may contain multiple function definitions), or directly
via flags. Note: --replace and --update are mutually exclusive.`,
	Example: `  faas-cli deploy -f https://domain/path/myfunctions.yml
  faas-cli deploy -f ./stack.yml
  faas-cli deploy -f ./stack.yml --label canary=true
  faas-cli deploy -f ./stack.yml --annotation user=true
  faas-cli deploy -f ./stack.yml --filter "*gif*" --secret dockerhuborg
  faas-cli deploy -f ./stack.yml --regex "fn[0-9]_.*"
  faas-cli deploy -f ./stack.yml --replace=false --update=true
  faas-cli deploy -f ./stack.yml --replace=true --update=false
  faas-cli deploy -f ./stack.yml --tag sha
  faas-cli deploy -f ./stack.yml --tag branch
  faas-cli deploy -f ./stack.yml --tag describe
  faas-cli deploy --image=alexellis/faas-url-ping --name=url-ping
  faas-cli deploy --image=my_image --name=my_fn --handler=/path/to/fn/
                  --gateway=http://remote-site.com:8080 --lang=python
                  --env=MYVAR=myval`,
	PreRunE: preRunDeploy,
	RunE:    runDeploy,
}

// preRunDeploy validates args & flags
func preRunDeploy(cmd *cobra.Command, args []string) error {
	language, _ = validateLanguageFlag(language)

	return nil
}

func runDeploy(cmd *cobra.Command, args []string) error {
	return runDeployCommand(args, image, fprocess, functionName, deployFlags, tagFormat)
}

func runDeployCommand(args []string, image string, fprocess string, functionName string, deployFlags DeployFlags, tagMode schema.BuildFormat) error {
	if deployFlags.update && deployFlags.replace {
		fmt.Println(`Cannot specify --update and --replace at the same time. One of --update or --replace must be false.
  --replace    removes an existing deployment before re-creating it
  --update     performs a rolling update to a new function image or configuration (default true)`)
		return fmt.Errorf("cannot specify --update and --replace at the same time")
	}

	var services stack.Services
	if len(yamlFile) > 0 {
		parsedServices, err := stack.ParseYAMLFile(yamlFile, regex, filter, envsubst)
		if err != nil {
			return err
		}

		parsedServices.Provider.GatewayURL = getGatewayURL(gateway, defaultGateway, parsedServices.Provider.GatewayURL, os.Getenv(openFaaSURLEnvironment))

		// Override network if passed
		if len(network) > 0 {
			parsedServices.Provider.Network = network
		}

		if parsedServices != nil {
			services = *parsedServices
		}
	}

	transport := GetDefaultCLITransport(tlsInsecure, &commandTimeout)
	ctx := context.Background()

	var failedStatusCodes = make(map[string]int)
	if len(services.Functions) > 0 {

		if len(services.Provider.Network) == 0 {
			services.Provider.Network = defaultNetwork
		}

		cliAuth := NewCLIAuth(token, services.Provider.GatewayURL)
		proxyClient := proxy.NewClient(cliAuth, services.Provider.GatewayURL, transport, &commandTimeout)

		for k, function := range services.Functions {

			functionSecrets := deployFlags.secrets

			function.Name = k
			fmt.Printf("Deploying: %s.\n", function.Name)

			var functionConstraints []string
			if function.Constraints != nil {
				functionConstraints = *function.Constraints
			} else if len(deployFlags.constraints) > 0 {
				functionConstraints = deployFlags.constraints
			}

			if len(function.Secrets) > 0 {
				functionSecrets = mergeSlice(function.Secrets, functionSecrets)
			}

			if deployFlags.sendRegistryAuth {

				dockerConfig := configFile{}
				err := readDockerConfig(&dockerConfig)
				if err != nil {
					log.Printf("Unable to read the docker config - %v", err.Error())
				}

				function.RegistryAuth = getRegistryAuth(&dockerConfig, function.Image)

			}

			fileEnvironment, err := readFiles(function.EnvironmentFile)
			if err != nil {
				return err
			}

			labelMap := map[string]string{}
			if function.Labels != nil {
				labelMap = *function.Labels
			}

			labelArgumentMap, labelErr := parseMap(deployFlags.labelOpts, "label")
			if labelErr != nil {
				return fmt.Errorf("error parsing labels: %v", labelErr)
			}

			allLabels := mergeMap(labelMap, labelArgumentMap)

			allEnvironment, envErr := compileEnvironment(deployFlags.envvarOpts, function.Environment, fileEnvironment)
			if envErr != nil {
				return envErr
			}

			if readTemplate {
				// Get FProcess to use from the ./template/template.yml, if a template is being used
				if languageExistsNotDockerfile(function.Language) {
					var fprocessErr error

					function.FProcess, fprocessErr = deriveFprocess(function)
					if fprocessErr != nil {
						return fmt.Errorf(`template directory may be missing or invalid, please run "faas-cli template pull"
Error: %s`, fprocessErr.Error())
					}
				}
			}

			functionResourceRequest := proxy.FunctionResourceRequest{
				Limits:   function.Limits,
				Requests: function.Requests,
			}

			var annotations map[string]string
			if function.Annotations != nil {
				annotations = *function.Annotations
			}

			annotationArgs, annotationErr := parseMap(deployFlags.annotationOpts, "annotation")
			if annotationErr != nil {
				return fmt.Errorf("error parsing annotations: %v", annotationErr)
			}

			allAnnotations := mergeMap(annotations, annotationArgs)

			branch, sha, err := builder.GetImageTagValues(tagMode)
			if err != nil {
				return err
			}

			function.Image = schema.BuildImageName(tagMode, function.Image, sha, branch)

			if deployFlags.readOnlyRootFilesystem {
				function.ReadOnlyRootFilesystem = deployFlags.readOnlyRootFilesystem
			}

			if len(function.QpsPerInstance) == 0 {
				function.QpsPerInstance = "10"
			}

			deploySpec := &proxy.DeployFunctionSpec{
				FProcess:                function.FProcess,
				FunctionName:            function.Name,
				Image:                   function.Image,
				QpsPerInstance:          function.QpsPerInstance,
				RegistryAuth:            function.RegistryAuth,
				Language:                function.Language,
				Replace:                 deployFlags.replace,
				EnvVars:                 allEnvironment,
				Network:                 services.Provider.Network,
				Constraints:             functionConstraints,
				Update:                  deployFlags.update,
				Secrets:                 functionSecrets,
				Labels:                  allLabels,
				Annotations:             allAnnotations,
				FunctionResourceRequest: functionResourceRequest,
				ReadOnlyRootFilesystem:  function.ReadOnlyRootFilesystem,
				TLSInsecure:             tlsInsecure,
				Token:                   token,
				Namespace:               function.Namespace,
			}

			if msg := checkTLSInsecure(services.Provider.GatewayURL, deploySpec.TLSInsecure); len(msg) > 0 {
				fmt.Println(msg)
			}
			statusCode := proxyClient.DeployFunction(ctx, deploySpec)
			if badStatusCode(statusCode) {
				failedStatusCodes[k] = statusCode
			}
		}
	} else {
		if len(image) == 0 || len(functionName) == 0 {
			return fmt.Errorf("To deploy a function give --yaml/-f or a --image and --name flag")
		}
		gateway = getGatewayURL(gateway, defaultGateway, "", os.Getenv(openFaaSURLEnvironment))
		cliAuth := NewCLIAuth(token, gateway)
		proxyClient := proxy.NewClient(cliAuth, gateway, transport, &commandTimeout)

		var registryAuth string
		if deployFlags.sendRegistryAuth {
			dockerConfig := configFile{}
			err := readDockerConfig(&dockerConfig)
			if err != nil {
				log.Printf("Unable to read the docker config - %v\n", err.Error())
			}

			registryAuth = getRegistryAuth(&dockerConfig, image)
		}
		// default to a readable filesystem until we get more input about the expected behavior
		// and if we want to add another flag for this case
		defaultReadOnlyRFS := false
		statusCode, err := deployImage(ctx, proxyClient, image, fprocess, functionName, registryAuth, deployFlags,
			tlsInsecure, defaultReadOnlyRFS, token, functionNamespace)
		if err != nil {
			return err
		}

		if badStatusCode(statusCode) {
			failedStatusCodes[functionName] = statusCode
		}
	}

	if err := deployFailed(failedStatusCodes); err != nil {
		return err
	}

	return nil
}

// deployImage deploys a function with the given image
func deployImage(
	ctx context.Context,
	client *proxy.Client,
	image string,
	fprocess string,
	functionName string,
	registryAuth string,
	deployFlags DeployFlags,
	tlsInsecure bool,
	readOnlyRootFilesystem bool,
	token string,
	namespace string,
) (int, error) {

	var statusCode int
	readOnlyRFS := deployFlags.readOnlyRootFilesystem || readOnlyRootFilesystem
	envvars, err := parseMap(deployFlags.envvarOpts, "env")

	if err != nil {
		return statusCode, fmt.Errorf("error parsing envvars: %v", err)
	}

	labelMap, labelErr := parseMap(deployFlags.labelOpts, "label")

	if labelErr != nil {
		return statusCode, fmt.Errorf("error parsing labels: %v", labelErr)
	}

	annotationMap, annotationErr := parseMap(deployFlags.annotationOpts, "annotation")

	if annotationErr != nil {
		return statusCode, fmt.Errorf("error parsing annotations: %v", annotationErr)
	}

	deploySpec := &proxy.DeployFunctionSpec{
		FProcess:                fprocess,
		FunctionName:            functionName,
		Image:                   image,
		RegistryAuth:            registryAuth,
		Language:                language,
		Replace:                 deployFlags.replace,
		EnvVars:                 envvars,
		Network:                 network,
		Constraints:             deployFlags.constraints,
		Update:                  deployFlags.update,
		Secrets:                 deployFlags.secrets,
		Labels:                  labelMap,
		Annotations:             annotationMap,
		FunctionResourceRequest: proxy.FunctionResourceRequest{},
		ReadOnlyRootFilesystem:  readOnlyRFS,
		TLSInsecure:             tlsInsecure,
		Token:                   token,
		Namespace:               namespace,
	}

	if msg := checkTLSInsecure(gateway, deploySpec.TLSInsecure); len(msg) > 0 {
		fmt.Println(msg)
	}

	statusCode = client.DeployFunction(ctx, deploySpec)

	return statusCode, nil
}

func mergeSlice(values []string, overlay []string) []string {
	results := []string{}
	added := make(map[string]bool)
	for _, value := range overlay {
		results = append(results, value)
		added[value] = true
	}

	for _, value := range values {
		if exists := added[value]; exists == false {
			results = append(results, value)
		}
	}

	return results
}

func readFiles(files []string) (map[string]string, error) {
	envs := make(map[string]string)

	for _, file := range files {
		bytesOut, readErr := ioutil.ReadFile(file)
		if readErr != nil {
			return nil, readErr
		}

		envFile := stack.EnvironmentFile{}
		unmarshalErr := yaml.Unmarshal(bytesOut, &envFile)
		if unmarshalErr != nil {
			return nil, unmarshalErr
		}
		for k, v := range envFile.Environment {
			envs[k] = v
		}
	}
	return envs, nil
}

func parseMap(envvars []string, keyName string) (map[string]string, error) {
	result := make(map[string]string)
	for _, envvar := range envvars {
		s := strings.SplitN(strings.TrimSpace(envvar), "=", 2)
		if len(s) != 2 {
			return nil, fmt.Errorf("label format is not correct, needs key=value")
		}
		envvarName := s[0]
		envvarValue := s[1]

		if !(len(envvarName) > 0) {
			return nil, fmt.Errorf("empty %s name: [%s]", keyName, envvar)
		}
		if !(len(envvarValue) > 0) {
			return nil, fmt.Errorf("empty %s value: [%s]", keyName, envvar)
		}

		result[envvarName] = envvarValue
	}
	return result, nil
}

func mergeMap(i map[string]string, j map[string]string) map[string]string {
	merged := make(map[string]string)

	for k, v := range i {
		merged[k] = v
	}
	for k, v := range j {
		merged[k] = v
	}
	return merged
}

func compileEnvironment(envvarOpts []string, yamlEnvironment map[string]string, fileEnvironment map[string]string) (map[string]string, error) {
	envvarArguments, err := parseMap(envvarOpts, "env")
	if err != nil {
		return nil, fmt.Errorf("error parsing envvars: %v", err)
	}

	functionAndStack := mergeMap(yamlEnvironment, fileEnvironment)
	return mergeMap(functionAndStack, envvarArguments), nil
}

func deriveFprocess(function stack.Function) (string, error) {
	var fprocess string

	pathToTemplateYAML := "./template/" + function.Language + "/template.yml"
	if _, err := os.Stat(pathToTemplateYAML); os.IsNotExist(err) {
		return "", err
	}

	var langTemplate stack.LanguageTemplate
	parsedLangTemplate, err := stack.ParseYAMLForLanguageTemplate(pathToTemplateYAML)

	if err != nil {
		return "", err

	}

	if parsedLangTemplate != nil {
		langTemplate = *parsedLangTemplate
		fprocess = langTemplate.FProcess
	}

	return fprocess, nil
}

func languageExistsNotDockerfile(language string) bool {
	return len(language) > 0 && strings.ToLower(language) != "dockerfile"
}

type authConfig struct {
	Auth string `json:"auth,omitempty"`
}

type configFile struct {
	AuthConfigs      map[string]authConfig `json:"auths"`
	CredentialsStore string                `json:"credsStore,omitempty"`
}

const (
	// docker default settings
	configFileName        = "config.json"
	configFileDir         = ".docker"
	defaultDockerRegistry = "https://index.docker.io/v1/"
)

var (
	configDir = os.Getenv("DOCKER_CONFIG")
)

func readDockerConfig(config *configFile) error {

	if configDir == "" {
		home, err := homedir.Dir()
		if err != nil {
			return err
		}
		configDir = filepath.Join(home, configFileDir)
	}
	filename := filepath.Join(configDir, configFileName)

	file, err := os.Open(filename)
	if err != nil {
		return err
	}
	defer file.Close()
	content, err := ioutil.ReadAll(file)
	if err != nil {
		return err
	}

	err = json.Unmarshal(content, config)
	if err != nil {
		return err
	}

	if config.CredentialsStore != "" {
		p := client.NewShellProgramFunc("docker-credential-" + config.CredentialsStore)

		for k := range config.AuthConfigs {
			creds, err := client.Get(p, k)
			if err != nil {
				return err
			}

			if config.AuthConfigs[k].Auth == "" {
				// apend base64 encoded "auth": "dGVzdDpQdXFxR3E2THZDYzhGQUwyUWtLcA==" (user:pass)
				registryAuth := creds.Username + ":" + creds.Secret
				registryAuth = base64.StdEncoding.EncodeToString([]byte(registryAuth))

				var tmp = config.AuthConfigs[k]
				tmp.Auth = registryAuth
				config.AuthConfigs[k] = tmp
			}
		}
	}
	return nil
}

func getRegistryAuth(config *configFile, image string) string {

	// The local library does not require an auth string.
	if strings.Contains(image, "/") == false {
		return ""
	}

	if len(config.AuthConfigs) > 0 {

		// image format is either:
		//   <docker registry>/<user>/<image>
		//   <docker registry>/<image>
		//   <user>/<image>

		// Registry value needs to be obtained / trimmed
		var registry string
		slashes := strings.Count(image, "/")
		if slashes > 1 {
			regS := strings.Split(image, "/")
			registry = regS[0]
		} else {
			if slashes == 1 {
				regS := strings.Split(image, "/")
				if strings.Contains(regS[0], ".") || strings.Contains(regS[0], ":") {
					registry = regS[0]
				}
			}
		}

		if registry != "" {
			return config.AuthConfigs[registry].Auth
		} else if (registry == "") && (config.AuthConfigs[defaultDockerRegistry].Auth != "") {
			return config.AuthConfigs[defaultDockerRegistry].Auth
		}
	}

	return ""
}

func deployFailed(status map[string]int) error {
	if len(status) == 0 {
		return nil
	}

	var allErrors []string
	for funcName, funcStatus := range status {
		err := fmt.Errorf("Function '%s' failed to deploy with status code: %d", funcName, funcStatus)
		allErrors = append(allErrors, err.Error())
	}
	return fmt.Errorf(strings.Join(allErrors, "\n"))
}

func badStatusCode(statusCode int) bool {
	return statusCode != http.StatusAccepted && statusCode != http.StatusOK
}

