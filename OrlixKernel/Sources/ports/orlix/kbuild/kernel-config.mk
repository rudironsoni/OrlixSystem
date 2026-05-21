SHELL := /bin/bash
.DEFAULT_GOAL := all

empty :=
space := $(empty) $(empty)
comma := ,

LINUX_VERSION ?= 6.12
LINUX_ARCH ?= orlix
LINUX_TAG ?= v$(LINUX_VERSION)
LINUX_REMOTE ?= https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git

PROFILE ?= appstore
ORLIX_PROFILES := appstore development

type ?= kunit
libc ?= linux

LINUX_UPSTREAM_DIR ?= OrlixKernel/Sources/upstream/linux-$(LINUX_VERSION)
LINUX_CLONE_CACHE_DIR ?= .cache/linux-clone
LINUX_CLONE_CACHE_REPO := $(CURDIR)/$(LINUX_CLONE_CACHE_DIR)/linux-$(LINUX_VERSION).git

ORLIX_LINUX_OVERLAY ?= OrlixKernel/Sources/ports/orlix/overlay
ORLIX_LINUX_PATCH_DIR ?= OrlixKernel/Sources/ports/orlix/patches
override ORLIX_PROFILE_CONFIG := OrlixKernel/Sources/ports/orlix/configs/$(PROFILE)_defconfig

ORLIX_KERNEL_PORT_DIR ?= Build/OrlixKernel/linux-$(LINUX_VERSION)-port
ORLIX_KERNEL_BUILD_ROOT := $(CURDIR)/Build/OrlixKernel/build
ORLIX_KERNEL_BUILD_DIR := $(ORLIX_KERNEL_BUILD_ROOT)/$(PROFILE)
ORLIX_KERNEL_ARCHIVE_ROOT := $(CURDIR)/Build/OrlixKernel/$(PROFILE)
ORLIX_KERNEL_ARCHIVE_MANIFEST := $(ORLIX_KERNEL_ARCHIVE_ROOT)/linux-object-manifest.txt
ORLIX_KERNEL_DEVICE_ARCHIVE_DIR := $(ORLIX_KERNEL_ARCHIVE_ROOT)/iphoneos
ORLIX_KERNEL_SIMULATOR_ARCHIVE_DIR := $(ORLIX_KERNEL_ARCHIVE_ROOT)/iphonesimulator
ORLIX_KERNEL_ARCHIVE_NAME := OrlixKernel.a
ORLIX_IOS_TARGET := arm64-apple-ios
ORLIX_IOS_SIMULATOR_TARGET := arm64-apple-ios-simulator
ORLIX_KERNEL_LINUX_SOURCES := arch/$(LINUX_ARCH)/boot/boot.c arch/$(LINUX_ARCH)/kernel/setup.c arch/$(LINUX_ARCH)/kernel/irq.c arch/$(LINUX_ARCH)/mm/delay.c drivers/base/devres.c fs/seq_file.c init/main.c kernel/cpu.c kernel/locking/mutex.c kernel/rcu/srcutiny.c kernel/rcu/update.c kernel/sched/swait.c kernel/sched/completion.c kernel/softirq.c kernel/panic.c kernel/printk/printk.c kernel/printk/printk_safe.c kernel/printk/printk_ringbuffer.c kernel/time/time.c kernel/time/timeconv.c lib/string.c lib/string_helpers.c lib/ctype.c lib/vsprintf.c lib/kstrtox.c lib/kasprintf.c lib/cmdline.c lib/errname.c lib/hexdump.c lib/uuid.c lib/siphash.c lib/ratelimit.c lib/find_bit.c lib/hweight.c lib/bitmap.c lib/math/int_sqrt.c mm/maccess.c mm/util.c mm/mmzone.c mm/memblock.c mm/slab_common.c mm/slub.c mm/show_mem.c mm/page_alloc.c mm/swap.c
ORLIX_KUNIT_BUILD_DIR := $(CURDIR)/Build/OrlixKernel/kunit/$(PROFILE)
ORLIX_TEMPORARY_KSELFTEST_INSTALL_DIR := $(CURDIR)/Build/OrlixKernel/kselftest/temporary/$(PROFILE)
ORLIX_TEMPORARY_TEST_INITRAMFS_DIR := $(CURDIR)/Build/OrlixKernel/test-initramfs/temporary/$(PROFILE)/OrlixTestInitramfs.bundle
ORLIX_MLIBC_KERNEL_HEADERS_DIR := $(CURDIR)/Build/OrlixMLibC/kernel-headers/$(PROFILE)
ORLIX_MLIBC_SYSROOT ?= Build/OrlixMLibC/sysroot/$(PROFILE)
ORLIX_MLIBC_KSELFTEST_INSTALL_DIR := $(CURDIR)/Build/OrlixMLibC/kselftest/$(PROFILE)
ORLIX_MLIBC_TEST_INITRAMFS_DIR := $(CURDIR)/Build/OrlixMLibC/test-initramfs/$(PROFILE)/OrlixTestInitramfs.bundle

ORLIX_LINUX_USERSPACE_SYSROOT_BOOTSTRAP_DIR ?= Build/OrlixKernel/linux-userspace-sysroot/aarch64
ORLIX_LINUX_USERSPACE_SYSROOT ?= $(ORLIX_LINUX_USERSPACE_SYSROOT_BOOTSTRAP_DIR)
ORLIX_KSELFTEST_ARCH ?= arm64

ORLIX_XCODE_PROJECT ?= OrlixSystem.xcodeproj
ORLIX_IOS_SIMULATOR_NAME ?= iPhone 17 Pro
ORLIX_IOS_SIMULATOR_ID ?=
ORLIX_IOS_SIMULATOR_DERIVED_DATA ?= $(CURDIR)/.deriveddata/OrlixSystem-sim
ORLIX_IOS_SIMULATOR_FRAMEWORK := $(ORLIX_IOS_SIMULATOR_DERIVED_DATA)/Build/Products/Debug-iphonesimulator/OrlixKernel.framework
ORLIX_KERNEL_PAYLOAD_DIR := $(CURDIR)/Build/OrlixKernel/payload/OrlixKernelPayload.bundle
ORLIX_KERNEL_XCFRAMEWORK ?= $(CURDIR)/Build/OrlixKernel/xcframework/OrlixKernel.xcframework
XCODEGEN ?= xcodegen
XCODEBUILD_MCP ?= xcodebuildmcp

LINUX_MAKE ?=
LINUX_SED ?=
LINUX_LLVM_BIN ?= $(shell if command -v llvm-ar >/dev/null 2>&1; then dirname "$$(command -v llvm-ar)"; elif [ -x /opt/homebrew/opt/llvm/bin/llvm-ar ]; then printf '%s\n' /opt/homebrew/opt/llvm/bin; fi)
LINUX_HOST_COMPAT_INCLUDE_ROOT := $(CURDIR)/tools/linux_host_compat/include
ORLIX_KERNEL_CC ?= clang
ORLIX_KERNEL_AR ?= llvm-ar
ORLIX_KERNEL_NM ?= nm
ORLIX_KERNEL_OTOOL ?= otool

TEST_TYPES := $(strip $(subst $(comma),$(space),$(type)))

ifeq ($(libc),linux)
KSELFTEST_SYSROOT := $(ORLIX_LINUX_USERSPACE_SYSROOT)
KSELFTEST_INSTALL_DIR := $(ORLIX_TEMPORARY_KSELFTEST_INSTALL_DIR)
KSELFTEST_INITRAMFS_DIR := $(ORLIX_TEMPORARY_TEST_INITRAMFS_DIR)
KSELFTEST_HEADER_FLAGS :=
KSELFTEST_PROOF_LABEL := temporary-kselftest-kernel-interface
ifeq ($(ORLIX_LINUX_USERSPACE_SYSROOT),$(ORLIX_LINUX_USERSPACE_SYSROOT_BOOTSTRAP_DIR))
KSELFTEST_PREREQS := __linux-userspace-sysroot prepare
else
KSELFTEST_PREREQS := prepare
endif
else ifeq ($(libc),orlixmlibc)
KSELFTEST_SYSROOT := $(ORLIX_MLIBC_SYSROOT)
KSELFTEST_INSTALL_DIR := $(ORLIX_MLIBC_KSELFTEST_INSTALL_DIR)
KSELFTEST_INITRAMFS_DIR := $(ORLIX_MLIBC_TEST_INITRAMFS_DIR)
KSELFTEST_HEADER_FLAGS := -isystem $(ORLIX_MLIBC_KERNEL_HEADERS_DIR)/include
KSELFTEST_PROOF_LABEL := orlixmlibc-kselftest-syscall-uapi
KSELFTEST_PREREQS := headers_install
else
KSELFTEST_SYSROOT :=
KSELFTEST_INSTALL_DIR :=
KSELFTEST_INITRAMFS_DIR :=
KSELFTEST_HEADER_FLAGS :=
KSELFTEST_PROOF_LABEL :=
KSELFTEST_PREREQS :=
endif
