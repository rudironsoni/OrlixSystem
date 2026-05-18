SHELL := /bin/bash
.DEFAULT_GOAL := all

PROJECT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT := $(abspath $(PROJECT_DIR)/..)
KERNEL_MAKE := $(MAKE) -C $(REPO_ROOT) -f OrlixKernel/Makefile

.PHONY: all help build test clean mrproper

all: build

help:
	@printf '%s\n' 'Targets:'
	@printf '%s\n' '  build     validate OrlixHostAdapter source ownership'
	@printf '%s\n' '  test      prepare the generated Xcode project for HostAdapter XCTest'
	@printf '%s\n' '  clean     no project-local generated outputs'

build:
	@printf '%s\n' 'OrlixHostAdapter sources are built through the app-hosted OrlixKernel Xcode target.'

test:
	@$(KERNEL_MAKE) xcodeproj
	@printf '%s\n' 'Run OrlixHostAdapterTests with XcodeBuildMCP from the repository root.'

clean:
	@true

mrproper: clean
