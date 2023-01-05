kubectl delete -f yaml/inuse
kubectl delete service/ssd -n openfaasdev-fn
kubectl delete service/mobilenet -n openfaasdev-fn
kubectl delete service/resnet-50 -n openfaasdev-fn
kubectl delete service/catdog -n openfaasdev-fn
kubectl delete service/textcnn-69 -n openfaasdev-fn
kubectl delete service/lstm-maxclass-2365 -n openfaasdev-fn
