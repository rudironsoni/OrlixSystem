SHELL := /bin/bash
.DEFAULT_GOAL := all

KERNEL_MAKE := $(MAKE) -f OrlixKernel/Makefile
HOSTADAPTER_MAKE := $(MAKE) -f OrlixHostAdapter/Makefile
MLIBC_MAKE := $(MAKE) -f OrlixMLibC/Makefile
TERMINAL_MAKE := $(MAKE) -f OrlixTerminal/Makefile

.PHONY: all help setup-env build prepare scripts dtbs headers_install kunit kselftest kselftest-install test xcodeproj run clean mrproper

all: build

help:
	@$(KERNEL_MAKE) help
	@printf '%s\n' ''
	@printf '%s\n' 'Project Makefiles:'
	@printf '%s\n' '  OrlixKernel/Makefile'
	@printf '%s\n' '  OrlixHostAdapter/Makefile'
	@printf '%s\n' '  OrlixMLibC/Makefile'
	@printf '%s\n' '  OrlixTerminal/Makefile'

setup-env xcodeproj:
	@$(KERNEL_MAKE) $@

build:
	@$(KERNEL_MAKE) build
	@$(HOSTADAPTER_MAKE) build
	@$(MLIBC_MAKE) build
	@$(TERMINAL_MAKE) build

prepare scripts dtbs kunit kselftest kselftest-install test:
	@$(KERNEL_MAKE) $@

headers_install:
	@$(MLIBC_MAKE) headers_install

run:
	@$(TERMINAL_MAKE) run

clean:
	@$(KERNEL_MAKE) clean
	@$(HOSTADAPTER_MAKE) clean
	@$(MLIBC_MAKE) clean
	@$(TERMINAL_MAKE) clean

mrproper:
	@$(KERNEL_MAKE) mrproper
	@$(HOSTADAPTER_MAKE) mrproper
	@$(MLIBC_MAKE) mrproper
	@$(TERMINAL_MAKE) mrproper
