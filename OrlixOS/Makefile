SHELL := /bin/bash
.DEFAULT_GOAL := all
.NOTPARALLEL:

PROJECT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT := $(abspath $(PROJECT_DIR)/..)
MLIBC_MAKE := $(MAKE) -C $(REPO_ROOT) -f OrlixMLibC/Makefile
KERNEL_MAKE := $(MAKE) -C $(REPO_ROOT) -f OrlixKernel/Makefile

PROFILE ?= release

include $(PROJECT_DIR)/Sources/distribution/manifest.mk

ifeq ($(PROFILE),release)
ORLIXOS_PROFILE_PACKAGES := $(ORLIXOS_RELEASE_PACKAGES)
ORLIXOS_PROFILE_ROOT_MODE := $(ORLIXOS_RELEASE_ROOT_MODE)
else
ORLIXOS_PROFILE_PACKAGES := $(ORLIXOS_DEVELOPMENT_PACKAGES)
ORLIXOS_PROFILE_ROOT_MODE := $(ORLIXOS_DEVELOPMENT_ROOT_MODE)
endif

include $(PROJECT_DIR)/Sources/make/config.mk

.PHONY: all help build rootfs test coreutils-test clean mrproper __coreutils-source FORCE

all: rootfs

help:
	@printf '%s\n' 'Targets:'
	@printf '%s\n' '  build    build OrlixOS userspace package inputs'
	@printf '%s\n' '  rootfs   build the immutable OrlixOS base root tree'
	@printf '%s\n' '  test     run OrlixOS package tests in the app-hosted Orlix path'
	@printf '%s\n' '  clean    remove generated OrlixOS outputs'

build: $(ORLIXOS_BASH_BINARY) $(ORLIXOS_COREUTILS_PROOF) $(ORLIXOS_FINDUTILS_PROOF) $(ORLIXOS_GETCONF_BINARY) $(ORLIXOS_GETENT_BINARY) $(ORLIXOS_INIT_BINARY)

rootfs: $(ORLIXOS_ROOTFS_PROOF)

include $(PROJECT_DIR)/Sources/make/rules.mk
