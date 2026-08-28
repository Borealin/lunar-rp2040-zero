SHELL := /bin/bash

PICO_SDK_PATH ?= $(CURDIR)/.deps/pico-sdk
BUILD_DIR ?= $(CURDIR)/build
OUTPUT_DIR ?= $(CURDIR)/build-host

export PICO_SDK_PATH
export BUILD_DIR
export OUTPUT_DIR

.PHONY: all sdk firmware host test vet verify install-service uninstall-service clean

all: firmware host

sdk:
	PICO_SDK_DIR="$(PICO_SDK_PATH)" ./scripts/bootstrap-pico-sdk.sh

firmware:
	@test -f "$(PICO_SDK_PATH)/pico_sdk_init.cmake" || \
		(echo 'Pico SDK missing; run make sdk or set PICO_SDK_PATH' >&2; exit 1)
	./scripts/build.sh

host:
	./scripts/build-host.sh

test:
	cd host/lunar-sensor-bridge && go test -race ./...

vet:
	cd host/lunar-sensor-bridge && go vet ./...

verify: test vet all

install-service: host
	./scripts/install-host-service.sh

uninstall-service:
	./scripts/uninstall-host-service.sh

clean:
	cmake -E rm -rf "$(BUILD_DIR)" "$(OUTPUT_DIR)"

