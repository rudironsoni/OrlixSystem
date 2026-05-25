SHELL := /bin/bash
.DEFAULT_GOAL := all

KERNEL_MAKE := $(MAKE) -f OrlixKernel/Makefile
HOSTADAPTER_MAKE := $(MAKE) -f OrlixHostAdapter/Makefile
MLIBC_MAKE := $(MAKE) -f OrlixMLibC/Makefile
ORLIXOS_MAKE := $(MAKE) -f OrlixOS/Makefile
TERMINAL_MAKE := $(MAKE) -f OrlixTerminal/Makefile
PROFILE ?= appstore
ORLIXOS_BASE_ROOT_TREE := $(CURDIR)/Build/OrlixOS/rootfs/$(PROFILE)/base-tree

.PHONY: all help setup-env build prepare scripts dtbs headers_install kunit kselftest kselftest-install test xcodeproj run clean mrproper

all: build

help:
	@$(KERNEL_MAKE) help
	@printf '%s\n' ''
	@printf '%s\n' 'Project Makefiles:'
	@printf '%s\n' '  OrlixKernel/Makefile'
	@printf '%s\n' '  OrlixHostAdapter/Makefile'
	@printf '%s\n' '  OrlixMLibC/Makefile'
	@printf '%s\n' '  OrlixOS/Makefile'
	@printf '%s\n' '  OrlixTerminal/Makefile'

setup-env xcodeproj:
	@$(KERNEL_MAKE) $@

build:
	@$(MAKE) clean
	@$(MLIBC_MAKE) build
	@$(ORLIXOS_MAKE) rootfs PROFILE="$(PROFILE)"
	@$(KERNEL_MAKE) build PROFILE="$(PROFILE)" ORLIX_KERNEL_BASE_ROOT_TREE_INPUT="$(ORLIXOS_BASE_ROOT_TREE)"
	@$(HOSTADAPTER_MAKE) build
	@$(TERMINAL_MAKE) build

prepare scripts dtbs kunit kselftest kselftest-install test:
	@$(KERNEL_MAKE) $@

headers_install:
	@$(MLIBC_MAKE) headers_install

run:
	@$(TERMINAL_MAKE) run PROFILE="$(PROFILE)" ORLIX_KERNEL_BASE_ROOT_TREE_INPUT="$(ORLIXOS_BASE_ROOT_TREE)"

clean:
	@$(KERNEL_MAKE) clean
	@$(HOSTADAPTER_MAKE) clean
	@$(MLIBC_MAKE) clean
	@$(ORLIXOS_MAKE) clean
	@$(TERMINAL_MAKE) clean

mrproper:
	@$(KERNEL_MAKE) mrproper
	@$(HOSTADAPTER_MAKE) mrproper
	@$(MLIBC_MAKE) mrproper
	@$(ORLIXOS_MAKE) mrproper
	@$(TERMINAL_MAKE) mrproper
