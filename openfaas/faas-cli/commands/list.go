// Copyright (c) Alex Ellis 2017. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

package commands

import (
	"context"
	"fmt"
	"os"

	"github.com/openfaas/faas-cli/proxy"
	"github.com/openfaas/faas-cli/stack"
	"github.com/spf13/cobra"
)

var (
	verboseList bool
	token       string
)

func init() {
	// Setup flags that are used by multiple commands (variables defined in faas.go)
	listCmd.Flags().StringVarP(&gateway, "gateway", "g", defaultGateway, "Gateway URL starting with http(s)://")
	listCmd.Flags().StringVarP(&functionNamespace, "namespace", "n", "", "Namespace of the function")
	listCmd.Flags().BoolVarP(&verboseList, "verbose", "v", false, "Verbose output for the function list")
	listCmd.Flags().BoolVar(&tlsInsecure, "tls-no-verify", false, "Disable TLS validation")
	listCmd.Flags().BoolVar(&envsubst, "envsubst", true, "Substitute environment variables in stack.yml file")
	listCmd.Flags().StringVarP(&token, "token", "k", "", "Pass a JWT token to use instead of basic auth")

	faasCmd.AddCommand(listCmd)
}

var listCmd = &cobra.Command{
	Use:     `list [--gateway GATEWAY_URL] [--verbose] [--tls-no-verify]`,
	Aliases: []string{"ls"},
	Short:   "List OpenFaaS functions",
	Long:    `Lists OpenFaaS functions either on a local or remote gateway`,
	Example: `  faas-cli list
  faas-cli list --gateway https://127.0.0.1:8080 --verbose`,
	RunE: runList,
}

func runList(cmd *cobra.Command, args []string) error {
	var services stack.Services
	var gatewayAddress string
	var yamlGateway string
	if len(yamlFile) > 0 {
		parsedServices, err := stack.ParseYAMLFile(yamlFile, regex, filter, envsubst)
		if err != nil {
			return err
		}

		if parsedServices != nil {
			services = *parsedServices
			yamlGateway = services.Provider.GatewayURL
		}
	}
	gatewayAddress = getGatewayURL(gateway, defaultGateway, yamlGateway, os.Getenv(openFaaSURLEnvironment))

	cliAuth := NewCLIAuth(token, gatewayAddress)
	transport := GetDefaultCLITransport(tlsInsecure, &commandTimeout)
	proxyClient := proxy.NewClient(cliAuth, gatewayAddress, transport, &commandTimeout)
	functions, err := proxyClient.ListFunctions(context.Background(), functionNamespace)
	if err != nil {
		return err
	}
        if verboseList {
		fmt.Printf("%-30s\t%-30s\t%-15s\t%-5s\t%-5s\n", "Function", "Image", "Invocations", "Replicas", "ServiceRate")
		for _, function := range functions {
			functionImage := function.Image
			if len(function.Image) > 30 {
				functionImage = functionImage[0:28] + ".."
			}
			serviceRate := fmt.Sprintf("%.2f", 100*function.SuccessSrvCount/function.InvocationCount)
                        serviceRate = serviceRate + "%"
			fmt.Printf("%-30s\t%-30s\t%-15d\t%-10d\t%-5s\n", function.Name, functionImage, int64(function.InvocationCount), function.Replicas, serviceRate)
		}
	} else {
		fmt.Printf("%-30s\t%-15s\t%-5s\n", "Function", "Invocations", "Replicas")
		for _, function := range functions {
			fmt.Printf("%-30s\t%-15d\t%-5d\n", function.Name, int64(function.InvocationCount), function.Replicas)

		}
	}
	return nil
}
