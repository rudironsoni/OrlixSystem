SHELL := /bin/bash
.DEFAULT_GOAL := all

PROJECT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT := $(abspath $(PROJECT_DIR)/..)
KERNEL_MAKE := $(MAKE) -C $(REPO_ROOT) -f OrlixKernel/Makefile

.PHONY: all help build headers_install kselftest clean mrproper

all: build

help:
	@printf '%s\n' 'Targets:'
	@printf '%s\n' '  build             placeholder until the OrlixMLibC sysroot exists'
	@printf '%s\n' '  headers_install   install Linux UAPI headers for OrlixMLibC'
	@printf '%s\n' '  kselftest         package OrlixMLibC-built kselftests'
	@printf '%s\n' '  clean             no project-local generated outputs'

build:
	@printf '%s\n' 'OrlixMLibC sysroot build is not implemented yet; headers_install is available.'

headers_install:
	@$(KERNEL_MAKE) headers_install

kselftest:
	@$(KERNEL_MAKE) kselftest libc=orlixmlibc

clean:
	@true

mrproper: clean
