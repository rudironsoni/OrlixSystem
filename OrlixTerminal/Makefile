SHELL := /bin/bash
.DEFAULT_GOAL := all

PROJECT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT := $(abspath $(PROJECT_DIR)/..)
KERNEL_MAKE := $(MAKE) -C $(REPO_ROOT) -f OrlixKernel/Makefile

.PHONY: all help build run clean mrproper

all: build

help:
	@printf '%s\n' 'Targets:'
	@printf '%s\n' '  build     validate OrlixTerminal source ownership'
	@printf '%s\n' '  run       build and run the OrlixTerminal iOS host app'
	@printf '%s\n' '  clean     no project-local generated outputs'

build:
	@printf '%s\n' 'OrlixTerminal is built through the generated OrlixTerminal Xcode scheme.'

run:
	@$(KERNEL_MAKE) run

clean:
	@true

mrproper: clean
