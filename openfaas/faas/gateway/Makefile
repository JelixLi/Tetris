.PHONY: all build push
TAG?=latest-dev
NS?=openfaas

all:	build
build:
	./build.sh ${TAG} ${NS}
push:
	./push.sh ${TAG} ${NS}
