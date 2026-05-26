.PHONY: build run clean

ifneq (,$(wildcard .env))
    include .env
    export
endif

NPROC := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)
JOBS ?= $(shell echo $$(($(NPROC) - 1)) | sed 's/^0$$/1/')

BUILD_DIR := build
CMAKE := cmake

build:
ifneq ($(Boost_ROOT),)
	@echo "Using Boost from $(Boost_ROOT)"
endif
	Boost_ROOT=$(Boost_ROOT) $(CMAKE) -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	$(CMAKE) --build $(BUILD_DIR) -j $(JOBS)
	rm -rf dist
	$(CMAKE) --install $(BUILD_DIR) --prefix dist

run: build
	./dist/bin/msg801

clean:
	rm -rf $(BUILD_DIR)
