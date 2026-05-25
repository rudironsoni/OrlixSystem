SHELL := /bin/bash
.DEFAULT_GOAL := all
.NOTPARALLEL:

PROJECT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT := $(abspath $(PROJECT_DIR)/..)
MLIBC_MAKE := $(MAKE) -C $(REPO_ROOT) -f OrlixMLibC/Makefile

PROFILE ?= appstore

BASH_VERSION ?= 5.3
BASH_URL ?= https://ftp.gnu.org/gnu/bash/bash-$(BASH_VERSION).tar.gz
BASH_SHA256 ?= 0d5cd86965f869a26cf64f4b71be7b96f90a3ba8b3d74e27e8e9d9d5550f31ba

ORLIXOS_BUILD_ROOT := $(REPO_ROOT)/Build/OrlixOS
ORLIXOS_UPSTREAM_DIR := $(ORLIXOS_BUILD_ROOT)/upstream
ORLIXOS_SRC_DIR := $(ORLIXOS_BUILD_ROOT)/src
ORLIXOS_PACKAGE_BUILD_DIR := $(ORLIXOS_BUILD_ROOT)/build/$(PROFILE)
ORLIXOS_PACKAGE_INSTALL_DIR := $(ORLIXOS_BUILD_ROOT)/packages/$(PROFILE)
ORLIXOS_ROOTFS_DIR := $(ORLIXOS_BUILD_ROOT)/rootfs/$(PROFILE)
ORLIXOS_BASE_ROOT_TREE := $(ORLIXOS_ROOTFS_DIR)/base-tree

ORLIXOS_BASH_ARCHIVE := $(ORLIXOS_UPSTREAM_DIR)/bash-$(BASH_VERSION).tar.gz
ORLIXOS_BASH_ARCHIVE_STAMP := $(ORLIXOS_UPSTREAM_DIR)/bash-$(BASH_VERSION).sha256-ok
ORLIXOS_BASH_SRC_DIR := $(ORLIXOS_SRC_DIR)/bash-$(BASH_VERSION)
ORLIXOS_BASH_SOURCE_STAMP := $(ORLIXOS_BASH_SRC_DIR)/.orlix-source-ready
ORLIXOS_BASH_BUILD_DIR := $(ORLIXOS_PACKAGE_BUILD_DIR)/bash-$(BASH_VERSION)
ORLIXOS_BASH_BINARY := $(ORLIXOS_PACKAGE_INSTALL_DIR)/usr/bin/bash
ORLIXOS_INIT_SOURCE := $(PROJECT_DIR)/Sources/init/init.c
ORLIXOS_INIT_BINARY := $(ORLIXOS_PACKAGE_INSTALL_DIR)/sbin/init
ORLIXOS_ROOTFS_PROOF := $(ORLIXOS_ROOTFS_DIR)/rootfs.proof

ORLIXOS_MLIBC_SYSROOT := $(REPO_ROOT)/Build/OrlixMLibC/sysroot/$(PROFILE)
ORLIXOS_MLIBC_HEADERS := $(REPO_ROOT)/Build/OrlixMLibC/kernel-headers/$(PROFILE)/include
ORLIXOS_MLIBC_RTLIB := $(REPO_ROOT)/Build/OrlixMLibC/compiler-rt/$(PROFILE)/liborlix_compiler_rt.a
ORLIXOS_HOSTED_USER_BASE_ADDRESS ?= 0x0000600000000000

ORLIXOS_CC ?= clang
ORLIXOS_LLVM_BIN ?= $(shell if command -v llvm-ar >/dev/null 2>&1; then dirname "$$(command -v llvm-ar)"; elif [ -x /opt/homebrew/opt/llvm/bin/llvm-ar ]; then printf '%s\n' /opt/homebrew/opt/llvm/bin; fi)
ORLIXOS_AR ?= $(if $(ORLIXOS_LLVM_BIN),$(ORLIXOS_LLVM_BIN)/llvm-ar,llvm-ar)
ORLIXOS_RANLIB ?= $(if $(ORLIXOS_LLVM_BIN),$(ORLIXOS_LLVM_BIN)/llvm-ranlib,llvm-ranlib)
ORLIXOS_STRIP ?= $(if $(ORLIXOS_LLVM_BIN),$(ORLIXOS_LLVM_BIN)/llvm-strip,llvm-strip)

.PHONY: all help build rootfs clean mrproper

all: rootfs

help:
	@printf '%s\n' 'Targets:'
	@printf '%s\n' '  build    build OrlixOS userspace package inputs'
	@printf '%s\n' '  rootfs   build the immutable OrlixOS base root tree'
	@printf '%s\n' '  clean    remove generated OrlixOS outputs'

build: $(ORLIXOS_BASH_BINARY) $(ORLIXOS_INIT_BINARY)

rootfs: $(ORLIXOS_ROOTFS_PROOF)

$(ORLIXOS_BASH_ARCHIVE):
	@set -euo pipefail; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_UPSTREAM_DIR)"; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked OrlixOS package path: $$path" >&2; exit 1; fi; \
	done; \
	command -v curl >/dev/null 2>&1 || { echo "curl is required to fetch Bash source" >&2; exit 1; }; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify Bash source" >&2; exit 1; }; \
	mkdir -p "$(ORLIXOS_UPSTREAM_DIR)"; \
	if [ ! -s "$(ORLIXOS_BASH_ARCHIVE)" ]; then curl -fL "$(BASH_URL)" -o "$(ORLIXOS_BASH_ARCHIVE)"; fi; \
	echo "upstream Bash archive ready: $(ORLIXOS_BASH_ARCHIVE)"

$(ORLIXOS_BASH_ARCHIVE_STAMP): $(ORLIXOS_BASH_ARCHIVE)
	@set -euo pipefail; \
	command -v shasum >/dev/null 2>&1 || { echo "shasum is required to verify Bash source" >&2; exit 1; }; \
	printf '%s  %s\n' "$(BASH_SHA256)" "$(ORLIXOS_BASH_ARCHIVE)" | shasum -a 256 -c - >/dev/null; \
	mkdir -p "$(dir $(ORLIXOS_BASH_ARCHIVE_STAMP))"; \
	touch "$(ORLIXOS_BASH_ARCHIVE_STAMP)"; \
	echo "upstream Bash ready: $(ORLIXOS_BASH_ARCHIVE)"

$(ORLIXOS_BASH_SOURCE_STAMP): $(ORLIXOS_BASH_ARCHIVE_STAMP)
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_BASH_SRC_DIR)"; \
	mkdir -p "$(ORLIXOS_SRC_DIR)"; \
	tar -xzf "$(ORLIXOS_BASH_ARCHIVE)" -C "$(ORLIXOS_SRC_DIR)"; \
	touch "$(ORLIXOS_BASH_SOURCE_STAMP)"; \
	echo "extracted Bash source: $(ORLIXOS_BASH_SRC_DIR)"

$(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB):
	@set -euo pipefail; \
	$(MLIBC_MAKE) build PROFILE="$(PROFILE)"; \
	[ -s "$(ORLIXOS_MLIBC_SYSROOT)/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $(ORLIXOS_MLIBC_SYSROOT)/usr/lib/libc.a" >&2; exit 1; }; \
	[ -s "$(ORLIXOS_MLIBC_RTLIB)" ] || { echo "missing Orlix compiler runtime archive: $(ORLIXOS_MLIBC_RTLIB)" >&2; exit 1; }

$(ORLIXOS_BASH_BINARY): $(ORLIXOS_BASH_SOURCE_STAMP) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build Bash; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_AR)" >/dev/null 2>&1 || { echo "llvm-ar is required to build Bash; set ORLIXOS_AR=/path/to/llvm-ar" >&2; exit 1; }; \
	command -v "$(ORLIXOS_RANLIB)" >/dev/null 2>&1 || { echo "llvm-ranlib is required to build Bash; set ORLIXOS_RANLIB=/path/to/llvm-ranlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package Bash; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	rm -rf "$(ORLIXOS_BASH_BUILD_DIR)" "$(ORLIXOS_BASH_BINARY)"; \
	mkdir -p "$(ORLIXOS_BASH_BUILD_DIR)" "$(dir $(ORLIXOS_BASH_BINARY))"; \
	cd "$(ORLIXOS_BASH_BUILD_DIR)"; \
	export CC="$(ORLIXOS_CC) --target=aarch64-linux-gnu --sysroot=$$sysroot -isystem $$headers -D_GNU_SOURCE -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie"; \
	export CFLAGS="-O2 -Wno-unknown-warning-option"; \
	export LDFLAGS="--target=aarch64-linux-gnu --sysroot=$$sysroot -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) $$sysroot/usr/lib/crt1.o $$sysroot/usr/lib/crti.o -Wl,--start-group"; \
	export LIBS="$$sysroot/usr/lib/libc.a $$sysroot/usr/lib/libm.a $$sysroot/usr/lib/libpthread.a $$sysroot/usr/lib/libssp_nonshared.a $$sysroot/usr/lib/libssp.a $$rtlib -Wl,--end-group $$sysroot/usr/lib/crtn.o"; \
	export AR="$(ORLIXOS_AR)"; \
	export RANLIB="$(ORLIXOS_RANLIB)"; \
	export bash_cv_getenv_redef=no; \
	export bash_cv_getcwd_malloc=yes; \
	export bash_cv_func_strchrnul_works=yes; \
	"$(ORLIXOS_BASH_SRC_DIR)/configure" --host=aarch64-linux-gnu --build="$$(uname -m)-apple-darwin" --prefix=/usr --without-bash-malloc --enable-static-link --disable-nls --disable-readline --without-installed-readline --without-curses; \
	$(MAKE) -j1 bash; \
	cp "$(ORLIXOS_BASH_BUILD_DIR)/bash" "$(ORLIXOS_BASH_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_BASH_BINARY)"; \
	file "$(ORLIXOS_BASH_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_BASH_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\npackage=bash\nversion=%s\nsha256=%s\n' "$(PROFILE)" "$(BASH_VERSION)" "$(BASH_SHA256)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/bash.proof"; \
	echo "built Orlix Linux Bash package input: $(ORLIXOS_BASH_BINARY)"

$(ORLIXOS_INIT_BINARY): $(ORLIXOS_INIT_SOURCE) $(ORLIXOS_MLIBC_SYSROOT)/.orlixmlibc-sysroot-ready $(ORLIXOS_MLIBC_RTLIB)
	@set -euo pipefail; \
	sysroot="$(ORLIXOS_MLIBC_SYSROOT)"; \
	headers="$(ORLIXOS_MLIBC_HEADERS)"; \
	rtlib="$(ORLIXOS_MLIBC_RTLIB)"; \
	[ -s "$$sysroot/usr/lib/libc.a" ] || { echo "missing OrlixMLibC libc archive: $$sysroot/usr/lib/libc.a" >&2; exit 1; }; \
	[ -d "$$headers" ] || { echo "missing Orlix Linux UAPI headers: $$headers" >&2; exit 1; }; \
	[ -s "$$rtlib" ] || { echo "missing Orlix compiler runtime archive: $$rtlib" >&2; exit 1; }; \
	command -v "$(ORLIXOS_CC)" >/dev/null 2>&1 || { echo "clang is required to build Orlix init; set ORLIXOS_CC=/path/to/clang" >&2; exit 1; }; \
	command -v "$(ORLIXOS_STRIP)" >/dev/null 2>&1 || { echo "llvm-strip is required to package Orlix init; set ORLIXOS_STRIP=/path/to/llvm-strip" >&2; exit 1; }; \
	mkdir -p "$(dir $(ORLIXOS_INIT_BINARY))"; \
	"$(ORLIXOS_CC)" --target=aarch64-linux-gnu --sysroot="$$sysroot" -isystem "$$headers" -D_GNU_SOURCE -std=c17 -O2 -fhosted -fno-builtin -femulated-tls -ffixed-x18 -fno-pie -static -fuse-ld=lld -nostdlib -Wl,--gc-sections -Wl,--image-base=$(ORLIXOS_HOSTED_USER_BASE_ADDRESS) "$$sysroot/usr/lib/crt1.o" "$$sysroot/usr/lib/crti.o" "$(ORLIXOS_INIT_SOURCE)" -Wl,--start-group "$$sysroot/usr/lib/libc.a" "$$sysroot/usr/lib/libm.a" "$$sysroot/usr/lib/libpthread.a" "$$sysroot/usr/lib/libssp_nonshared.a" "$$sysroot/usr/lib/libssp.a" "$$rtlib" -Wl,--end-group "$$sysroot/usr/lib/crtn.o" -o "$(ORLIXOS_INIT_BINARY)"; \
	"$(ORLIXOS_STRIP)" "$(ORLIXOS_INIT_BINARY)"; \
	file "$(ORLIXOS_INIT_BINARY)" | grep -F -q 'ELF 64-bit LSB executable, ARM aarch64' || { file "$(ORLIXOS_INIT_BINARY)" >&2; exit 1; }; \
	printf 'profile=%s\nprogram=init\nconsole=/dev/hvc0\nshell=/bin/sh\n' "$(PROFILE)" > "$(ORLIXOS_PACKAGE_INSTALL_DIR)/init.proof"; \
	echo "built OrlixOS first-stage init: $(ORLIXOS_INIT_BINARY)"

$(ORLIXOS_ROOTFS_PROOF): $(ORLIXOS_BASH_BINARY) $(ORLIXOS_INIT_BINARY)
	@set -euo pipefail; \
	root_tree="$(ORLIXOS_BASE_ROOT_TREE)"; \
	case "$$root_tree" in "$(REPO_ROOT)"/Build/OrlixOS/rootfs/*/base-tree) ;; *) echo "refusing to write OrlixOS root tree outside Build/OrlixOS/rootfs: $$root_tree" >&2; exit 1 ;; esac; \
	for path in "$(REPO_ROOT)/Build" "$(ORLIXOS_BUILD_ROOT)" "$(ORLIXOS_ROOTFS_DIR)" "$$root_tree"; do \
		if [ -L "$$path" ]; then echo "refusing to package OrlixOS root tree through symlinked path: $$path" >&2; exit 1; fi; \
	done; \
	rm -rf "$$root_tree"; \
	mkdir -p "$$root_tree/bin" "$$root_tree/dev" "$$root_tree/etc" "$$root_tree/proc" "$$root_tree/root" "$$root_tree/run" "$$root_tree/sbin" "$$root_tree/sys" "$$root_tree/tmp" "$$root_tree/usr/bin" "$$root_tree/var/tmp"; \
	install -m 0755 "$(ORLIXOS_BASH_BINARY)" "$$root_tree/bin/bash"; \
	install -m 0755 "$(ORLIXOS_INIT_BINARY)" "$$root_tree/sbin/init"; \
	ln -s bash "$$root_tree/bin/sh"; \
	printf '%s\n' 'root:x:0:0:root:/root:/bin/sh' > "$$root_tree/etc/passwd"; \
	printf '%s\n' 'root:x:0:root' > "$$root_tree/etc/group"; \
	printf '%s\n' 'NAME=OrlixOS' 'ID=orlix' 'PRETTY_NAME=OrlixOS' > "$$root_tree/etc/os-release"; \
	chmod 0755 "$$root_tree" "$$root_tree/bin" "$$root_tree/dev" "$$root_tree/etc" "$$root_tree/proc" "$$root_tree/run" "$$root_tree/sbin" "$$root_tree/sys" "$$root_tree/usr" "$$root_tree/usr/bin" "$$root_tree/var"; \
	chmod 0700 "$$root_tree/root"; \
	chmod 1777 "$$root_tree/tmp" "$$root_tree/var/tmp"; \
	printf 'profile=%s\ninit=/sbin/init\nshell=/bin/sh\npackage=bash\nversion=%s\n' "$(PROFILE)" "$(BASH_VERSION)" > "$(ORLIXOS_ROOTFS_PROOF)"; \
	echo "built OrlixOS base root tree: $$root_tree"

clean:
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_BUILD_ROOT)/build" "$(ORLIXOS_BUILD_ROOT)/packages" "$(ORLIXOS_BUILD_ROOT)/rootfs" "$(ORLIXOS_SRC_DIR)"

mrproper: clean
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_BUILD_ROOT)"
