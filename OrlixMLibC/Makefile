SHELL := /bin/bash
.DEFAULT_GOAL := all

PROJECT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT := $(abspath $(PROJECT_DIR)/..)
KERNEL_MAKE := $(MAKE) -C $(REPO_ROOT) -f OrlixKernel/Makefile

PROFILE ?= appstore

MLIBC_REMOTE ?= https://github.com/managarm/mlibc.git
MLIBC_COMMIT ?= 43ab07732cdfefe2e915bf98190f5d7f4bd8d765
MLIBC_UPSTREAM_DIR := $(REPO_ROOT)/Build/OrlixMLibC/upstream/mlibc
MLIBC_BUILD_DIR := $(REPO_ROOT)/Build/OrlixMLibC/build/$(PROFILE)
MLIBC_SYSROOT := $(REPO_ROOT)/Build/OrlixMLibC/sysroot/$(PROFILE)
MLIBC_CROSS_FILE := $(REPO_ROOT)/Build/OrlixMLibC/cross/$(PROFILE)-orlix-aarch64-linux.cross
MLIBC_PATCH_DIR := $(PROJECT_DIR)/Sources/patches
MLIBC_KERNEL_HEADERS := $(REPO_ROOT)/Build/OrlixMLibC/kernel-headers/$(PROFILE)/include
ORLIX_HOSTED_SYSCALL_GATE_ADDRESS ?= 0x00006ffff0000000
MLIBC_LLVM_PREFIX ?= $(firstword $(wildcard /opt/homebrew/opt/llvm/bin) $(wildcard /usr/local/opt/llvm/bin))
MLIBC_CC ?= clang
MLIBC_CXX ?= clang++
MLIBC_AR ?= $(if $(MLIBC_LLVM_PREFIX),$(MLIBC_LLVM_PREFIX)/llvm-ar,llvm-ar)
MLIBC_STRIP ?= $(if $(MLIBC_LLVM_PREFIX),$(MLIBC_LLVM_PREFIX)/llvm-strip,llvm-strip)
MESON ?= meson
NINJA ?= ninja

.PHONY: all help build headers_install kselftest clean mrproper __bootstrap-mlibc __apply-mlibc-patches __write-cross-file

all: build

help:
	@printf '%s\n' 'Targets:'
	@printf '%s\n' '  build             build OrlixMLibC from upstream mlibc sysdeps/linux'
	@printf '%s\n' '  headers_install   install Linux UAPI headers for OrlixMLibC'
	@printf '%s\n' '  kselftest         package OrlixMLibC-built kselftests'
	@printf '%s\n' '  clean             remove generated OrlixMLibC outputs'

build: headers_install __bootstrap-mlibc __apply-mlibc-patches __write-cross-file
	@set -euo pipefail; \
	command -v "$(MESON)" >/dev/null 2>&1 || { echo "meson is required to build OrlixMLibC" >&2; exit 1; }; \
	command -v "$(NINJA)" >/dev/null 2>&1 || { echo "ninja is required to build OrlixMLibC" >&2; exit 1; }; \
	[ -d "$(MLIBC_KERNEL_HEADERS)" ] || { echo "missing installed Orlix Linux UAPI headers: $(MLIBC_KERNEL_HEADERS)" >&2; exit 1; }; \
	rm -rf "$(MLIBC_BUILD_DIR)" "$(MLIBC_SYSROOT)"; \
	mkdir -p "$(MLIBC_BUILD_DIR)" "$(MLIBC_SYSROOT)"; \
	"$(MESON)" setup "$(MLIBC_BUILD_DIR)" "$(MLIBC_UPSTREAM_DIR)" \
		--cross-file "$(MLIBC_CROSS_FILE)" \
		--prefix /usr \
		-Ddefault_library=static \
		-Dheaders_only=false \
		-Dno_headers=false \
		-Dbuild_tests=false \
		-Dlibgcc_dependency=false \
		-Dlinux_option=enabled \
		-Dposix_option=enabled \
		-Dglibc_option=enabled \
		-Dbsd_option=enabled \
		-Dlinux_kernel_headers="$(MLIBC_KERNEL_HEADERS)"; \
	"$(MESON)" compile -C "$(MLIBC_BUILD_DIR)"; \
	DESTDIR="$(MLIBC_SYSROOT)" "$(MESON)" install -C "$(MLIBC_BUILD_DIR)"; \
	[ -d "$(MLIBC_SYSROOT)/usr/include" ] || { echo "missing OrlixMLibC installed headers: $(MLIBC_SYSROOT)/usr/include" >&2; exit 1; }; \
	[ -d "$(MLIBC_SYSROOT)/usr/lib" ] || { echo "missing OrlixMLibC installed libraries: $(MLIBC_SYSROOT)/usr/lib" >&2; exit 1; }; \
	printf 'profile=%s\nmlibc_commit=%s\n' "$(PROFILE)" "$(MLIBC_COMMIT)" > "$(MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready"; \
	echo "built OrlixMLibC sysroot: $(MLIBC_SYSROOT)"

headers_install:
	@$(KERNEL_MAKE) headers_install PROFILE="$(PROFILE)"

kselftest: build
	@$(KERNEL_MAKE) kselftest PROFILE="$(PROFILE)" libc=orlixmlibc

clean:
	@set -euo pipefail; \
	for path in \
		"$(REPO_ROOT)/Build/OrlixMLibC/build" \
		"$(REPO_ROOT)/Build/OrlixMLibC/cross" \
		"$(REPO_ROOT)/Build/OrlixMLibC/kselftest" \
		"$(REPO_ROOT)/Build/OrlixMLibC/sysroot" \
		"$(REPO_ROOT)/Build/OrlixMLibC/test-initramfs" \
		"$(REPO_ROOT)/Build/OrlixMLibC/upstream"; do \
		if [ -L "$$path" ]; then echo "refusing to clean symlinked path: $$path" >&2; exit 1; fi; \
		rm -rf "$$path"; \
	done; \
	echo "cleaned generated OrlixMLibC outputs"

mrproper: clean

__bootstrap-mlibc:
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(REPO_ROOT)/Build/OrlixMLibC" "$(REPO_ROOT)/Build/OrlixMLibC/upstream" "$(MLIBC_UPSTREAM_DIR)"; do \
		if [ -L "$$path" ]; then echo "refusing to use symlinked mlibc path: $$path" >&2; exit 1; fi; \
	done; \
	rm -rf "$(MLIBC_UPSTREAM_DIR)"; \
	mkdir -p "$(dir $(MLIBC_UPSTREAM_DIR))"; \
	git clone --no-checkout "$(MLIBC_REMOTE)" "$(MLIBC_UPSTREAM_DIR)"; \
	git -C "$(MLIBC_UPSTREAM_DIR)" checkout --quiet --detach "$(MLIBC_COMMIT)"; \
	checked_commit="$$(git -C "$(MLIBC_UPSTREAM_DIR)" rev-parse HEAD)"; \
	if [ "$$checked_commit" != "$(MLIBC_COMMIT)" ]; then \
		echo "expected mlibc commit $(MLIBC_COMMIT), checked out $$checked_commit" >&2; \
		exit 1; \
	fi; \
	echo "upstream mlibc ready: $(MLIBC_UPSTREAM_DIR) ($$checked_commit)"

__apply-mlibc-patches:
	@set -euo pipefail; \
	[ -d "$(MLIBC_UPSTREAM_DIR)/.git" ] || { echo "missing upstream mlibc clone: $(MLIBC_UPSTREAM_DIR)" >&2; exit 1; }; \
	if [ -d "$(MLIBC_PATCH_DIR)" ]; then \
		for patch in "$(MLIBC_PATCH_DIR)"/*.patch; do \
			[ -e "$$patch" ] || continue; \
			git -C "$(MLIBC_UPSTREAM_DIR)" apply --check "$$patch"; \
			git -C "$(MLIBC_UPSTREAM_DIR)" apply "$$patch"; \
		done; \
	fi; \
	git -C "$(MLIBC_UPSTREAM_DIR)" diff --check; \
	echo "applied OrlixMLibC upstream mlibc patches"

__write-cross-file:
	@set -euo pipefail; \
	mkdir -p "$(dir $(MLIBC_CROSS_FILE))"; \
	{ \
		printf '%s\n' '[binaries]'; \
		printf "c = ['%s', '--target=aarch64-linux-gnu']\n" "$(MLIBC_CC)"; \
		printf "cpp = ['%s', '--target=aarch64-linux-gnu']\n" "$(MLIBC_CXX)"; \
		printf "ar = '%s'\n" "$(MLIBC_AR)"; \
		printf "strip = '%s'\n" "$(MLIBC_STRIP)"; \
		printf '%s\n' ''; \
		printf '%s\n' '[host_machine]'; \
		printf "%s\n" "system = 'linux'"; \
		printf "%s\n" "cpu_family = 'aarch64'"; \
		printf "%s\n" "cpu = 'aarch64'"; \
		printf "%s\n" "endian = 'little'"; \
		printf '%s\n' ''; \
		printf '%s\n' '[properties]'; \
		printf '%s\n' 'needs_exe_wrapper = true'; \
		printf '%s\n' ''; \
		printf '%s\n' '[built-in options]'; \
		printf "%s\n" "c_args = ['-DORLIX_HOSTED_SYSCALL_GATE_ADDRESS=$(ORLIX_HOSTED_SYSCALL_GATE_ADDRESS)', '-ffunction-sections', '-fdata-sections']"; \
		printf "%s\n" "cpp_args = ['-DORLIX_HOSTED_SYSCALL_GATE_ADDRESS=$(ORLIX_HOSTED_SYSCALL_GATE_ADDRESS)', '-ffunction-sections', '-fdata-sections']"; \
		printf "%s\n" "c_link_args = ['-fuse-ld=lld']"; \
		printf "%s\n" "cpp_link_args = ['-fuse-ld=lld']"; \
	} > "$(MLIBC_CROSS_FILE)"; \
	echo "wrote OrlixMLibC mlibc cross file: $(MLIBC_CROSS_FILE)"
