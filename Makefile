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

LINUX_UPSTREAM_DIR ?= Linux/upstream/linux-$(LINUX_VERSION)
LINUX_CLONE_CACHE_DIR ?= .cache/linux-clone
LINUX_CLONE_CACHE_REPO := $(CURDIR)/$(LINUX_CLONE_CACHE_DIR)/linux-$(LINUX_VERSION).git

ORLIX_LINUX_OVERLAY ?= Linux/ports/orlix/overlay
ORLIX_LINUX_PATCH_DIR ?= Linux/ports/orlix/patches
override ORLIX_PROFILE_CONFIG := Linux/ports/orlix/configs/$(PROFILE)_defconfig

ORLIX_KERNEL_PORT_DIR ?= Build/OrlixKernel/linux-$(LINUX_VERSION)-port
ORLIX_KERNEL_BUILD_ROOT := $(CURDIR)/Build/OrlixKernel/build
ORLIX_KERNEL_BUILD_DIR := $(ORLIX_KERNEL_BUILD_ROOT)/$(PROFILE)
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
ORLIX_KERNEL_XCFRAMEWORK ?= $(CURDIR)/Build/OrlixKernel/xcframework/OrlixKernel.xcframework
XCODEGEN ?= xcodegen
XCODEBUILD_MCP ?= xcodebuildmcp

LINUX_MAKE ?=
LINUX_SED ?=
LINUX_LLVM_BIN ?= $(shell if command -v llvm-ar >/dev/null 2>&1; then dirname "$$(command -v llvm-ar)"; elif [ -x /opt/homebrew/opt/llvm/bin/llvm-ar ]; then printf '%s\n' /opt/homebrew/opt/llvm/bin; fi)
LINUX_HOST_COMPAT_INCLUDE_ROOT := $(CURDIR)/tools/linux_host_compat/include

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

.PHONY: all setup-env build test clean mrproper help prepare scripts dtbs headers_install kunit kselftest kselftest-install xcodeproj run __bootstrap-linux-upstream __validate-profile __prepare-port __prepare-kbuild __headers-install __kunit __linux-userspace-sysroot __kselftest-install __kselftest-initramfs __ios-simulator-framework __ios-simulator-xcframework

all: build

help:
	@printf '%s\n' 'Targets:'
	@printf '%s\n' '  setup-env                 fetch upstream Linux and generate the Xcode project'
	@printf '%s\n' '  build                     build the app-hosted OrlixKernel iOS artifact'
	@printf '%s\n' '  test                      run test type(s), default: type=kunit'
	@printf '%s\n' '  test type=kunit           build Linux KUnit-selected Orlix tests'
	@printf '%s\n' '  test type=kunit,kselftest build KUnit and Linux kselftest artifacts'
	@printf '%s\n' '  kselftest libc=linux      install and package temporary Linux-libc kselftests'
	@printf '%s\n' '  kselftest libc=orlixmlibc install and package OrlixMLibC kselftests'
	@printf '%s\n' '  headers_install           install Linux UAPI headers for OrlixMLibC'
	@printf '%s\n' '  clean                     remove normal generated outputs'
	@printf '%s\n' '  mrproper                  remove all generated local outputs'

setup-env: __bootstrap-linux-upstream xcodeproj

build: __ios-simulator-xcframework

prepare scripts dtbs: __prepare-kbuild

headers_install: __headers-install

kunit: __kunit

kselftest-install: $(KSELFTEST_PREREQS) __kselftest-install

kselftest: kselftest-install __kselftest-initramfs

test:
	@set -euo pipefail; \
	if [ -z "$(TEST_TYPES)" ]; then \
		echo "type must name at least one test class: kunit,kselftest" >&2; \
		exit 1; \
	fi; \
	for selected in $(TEST_TYPES); do \
		case "$$selected" in \
			kunit) $(MAKE) kunit PROFILE="$(PROFILE)" ;; \
			kselftest) $(MAKE) kselftest PROFILE="$(PROFILE)" libc="$(libc)" ;; \
			*) echo "unsupported test type: $$selected (expected kunit or kselftest)" >&2; exit 1 ;; \
		esac; \
	done

xcodeproj:
	@set -euo pipefail; \
	command -v "$(XCODEGEN)" >/dev/null 2>&1 || { echo "XcodeGen is required; install xcodegen or set XCODEGEN=/path/to/xcodegen" >&2; exit 1; }; \
	"$(XCODEGEN)" generate --spec project.yml

run: xcodeproj
	@set -euo pipefail; \
	command -v "$(XCODEBUILD_MCP)" >/dev/null 2>&1 || { echo "XcodeBuildMCP is required; install xcodebuildmcp or set XCODEBUILD_MCP=/path/to/xcodebuildmcp" >&2; exit 1; }; \
	selector=(); \
	if [ -n "$(ORLIX_IOS_SIMULATOR_ID)" ]; then \
		selector=(--simulator-id "$(ORLIX_IOS_SIMULATOR_ID)"); \
	else \
		selector=(--simulator-name "$(ORLIX_IOS_SIMULATOR_NAME)" --use-latest-os); \
	fi; \
	"$(XCODEBUILD_MCP)" simulator build-and-run \
		--project-path "$(CURDIR)/$(ORLIX_XCODE_PROJECT)" \
		--scheme "OrlixTerminal" \
		--configuration "Debug" \
		--derived-data-path "$(ORLIX_IOS_SIMULATOR_DERIVED_DATA)" \
		"$${selector[@]}" \
		--output json

clean:
	@set -euo pipefail; \
	for path in \
		Build/OrlixKernel/build \
		Build/OrlixKernel/kunit \
		Build/OrlixKernel/kselftest \
		Build/OrlixKernel/test-initramfs \
		Build/OrlixKernel/tool-shims \
		Build/OrlixKernel/xcframework \
		Build/OrlixMLibC/kernel-headers \
		Build/OrlixMLibC/kselftest \
		Build/OrlixMLibC/test-initramfs \
		.deriveddata/OrlixSystem-sim; do \
		if [ -L "$$path" ]; then echo "refusing to clean symlinked path: $$path" >&2; exit 1; fi; \
		rm -rf "$$path"; \
	done; \
	echo "cleaned generated build outputs"

mrproper: clean
	@set -euo pipefail; \
	for path in Build .deriveddata OrlixSystem.xcodeproj Linux/upstream; do \
		if [ -L "$$path" ]; then echo "refusing to remove symlinked path: $$path" >&2; exit 1; fi; \
		rm -rf "$$path"; \
	done; \
	echo "removed generated local outputs"

__bootstrap-linux-upstream:
	@set -euo pipefail; \
	linux_remote="$(LINUX_REMOTE)"; \
	linux_tag="$(LINUX_TAG)"; \
	upstream_dir="$(LINUX_UPSTREAM_DIR)"; \
	clone_cache="$(LINUX_CLONE_CACHE_REPO)"; \
	expected_upstream_dir="Linux/upstream/linux-$(LINUX_VERSION)"; \
	expected_clone_cache="$(CURDIR)/.cache/linux-clone/linux-$(LINUX_VERSION).git"; \
	if [ "$$upstream_dir" != "$$expected_upstream_dir" ]; then \
		echo "Linux upstream directory must be $$expected_upstream_dir: $$upstream_dir" >&2; \
		exit 1; \
	fi; \
	if [ "$$clone_cache" != "$$expected_clone_cache" ]; then \
		echo "Linux clone cache repo must be $$expected_clone_cache: $$clone_cache" >&2; \
		exit 1; \
	fi; \
	for path in Build Linux Linux/upstream .cache .cache/linux-clone "$$upstream_dir" "$$clone_cache"; do \
		if [ -L "$$path" ]; then echo "refusing to use symlinked path: $$path" >&2; exit 1; fi; \
	done; \
	if [ -d "$$upstream_dir/.git" ]; then \
		checked_tag="$$(git -C "$$upstream_dir" describe --tags --exact-match HEAD 2>/dev/null || true)"; \
		if [ "$$checked_tag" = "$$linux_tag" ]; then \
			echo "upstream Linux ready: $$upstream_dir ($$checked_tag)"; \
			exit 0; \
		fi; \
	fi; \
	mkdir -p "$$(dirname "$$upstream_dir")" "$$(dirname "$$clone_cache")"; \
	if [ ! -d "$$clone_cache/objects" ]; then \
		rm -rf "$$clone_cache"; \
		git init --bare "$$clone_cache" >/dev/null; \
		git -C "$$clone_cache" remote add origin "$$linux_remote"; \
	else \
		git -C "$$clone_cache" remote set-url origin "$$linux_remote"; \
	fi; \
	git -C "$$clone_cache" fetch --force --depth 1 origin "refs/tags/$$linux_tag:refs/tags/$$linux_tag"; \
	rm -rf "$$upstream_dir"; \
	git clone --shared --no-checkout --origin origin "$$clone_cache" "$$upstream_dir"; \
	git -C "$$upstream_dir" -c advice.detachedHead=false checkout --quiet --force --detach "$$linux_tag"; \
	checked_tag="$$(git -C "$$upstream_dir" describe --tags --exact-match HEAD)"; \
	checked_commit="$$(git -C "$$upstream_dir" rev-parse HEAD)"; \
	tag_commit="$$(git -C "$$clone_cache" rev-list -n1 "$$linux_tag")"; \
	if [ "$$checked_tag" != "$$linux_tag" ] || [ "$$checked_commit" != "$$tag_commit" ]; then \
		echo "expected $$linux_tag at $$tag_commit but checked out $$checked_tag at $$checked_commit" >&2; \
		exit 1; \
	fi; \
	echo "upstream Linux ready: $$upstream_dir ($$checked_tag, clone cache $$clone_cache)"

__validate-profile:
	@set -euo pipefail; \
	profile="$(PROFILE)"; \
	case " $(ORLIX_PROFILES) " in \
		*" $$profile "*) ;; \
		*) echo "unsupported PROFILE=$$profile (expected one of: $(ORLIX_PROFILES))" >&2; exit 1 ;; \
	esac; \
	config="$(ORLIX_PROFILE_CONFIG)"; \
	[ -f "$$config" ] || { echo "missing profile defconfig: $$config" >&2; exit 1; }

__prepare-port: __validate-profile __bootstrap-linux-upstream
	@set -euo pipefail; \
	upstream_dir="$(LINUX_UPSTREAM_DIR)"; \
	port_dir="$(ORLIX_KERNEL_PORT_DIR)"; \
	expected_port_dir="Build/OrlixKernel/linux-$(LINUX_VERSION)-port"; \
	overlay_dir="$(ORLIX_LINUX_OVERLAY)"; \
	patch_dir="$(ORLIX_LINUX_PATCH_DIR)"; \
	profile_config="$(ORLIX_PROFILE_CONFIG)"; \
	exception_dir="$$patch_dir/exceptions"; \
	forbidden_re='^(fs|kernel|mm|ipc|net|include/linux|include/uapi)(/|$$)'; \
	if [ "$$port_dir" != "$$expected_port_dir" ]; then \
		echo "Orlix kernel port tree must be $$expected_port_dir: $$port_dir" >&2; \
		exit 1; \
	fi; \
	if [ "$$port_dir" = "$$upstream_dir" ]; then \
		echo "Orlix kernel port tree must not equal upstream directory: $$port_dir" >&2; \
		exit 1; \
	fi; \
	for path in Build Build/OrlixKernel; do \
		if [ -L "$$path" ]; then echo "refusing to use symlinked path: $$path" >&2; exit 1; fi; \
	done; \
	[ -d "$$overlay_dir" ] || { echo "missing Linux overlay directory: $$overlay_dir" >&2; exit 1; }; \
	rm -rf "$$port_dir"; \
	mkdir -p "$$port_dir"; \
	git -C "$$upstream_dir" archive --format=tar HEAD | tar -x -C "$$port_dir"; \
	cp -R "$$overlay_dir/." "$$port_dir"; \
	mkdir -p "$$port_dir/arch/$(LINUX_ARCH)/configs"; \
	cp "$$profile_config" "$$port_dir/arch/$(LINUX_ARCH)/configs/defconfig"; \
	if [ -d "$$patch_dir" ]; then \
		for patch in "$$patch_dir"/*.patch "$$patch_dir"/*.diff; do \
			[ -e "$$patch" ] || continue; \
			patch_name="$$(basename "$$patch")"; \
			case "$$patch" in \
				/*) patch_abs="$$patch" ;; \
				*) patch_abs="$(CURDIR)/$$patch" ;; \
			esac; \
			if awk -v re="$$forbidden_re" '/^\+\+\+ b\// { path = substr($$2, 3); if (path ~ re) bad = 1 } END { exit bad ? 0 : 1 }' "$$patch_abs"; then \
				if [ ! -f "$$exception_dir/$$patch_name.md" ]; then \
					echo "patch $$patch_name touches a forbidden upstream path; add $$exception_dir/$$patch_name.md" >&2; \
					exit 1; \
				fi; \
			fi; \
			patch -d "$$port_dir" -p1 < "$$patch_abs" >/dev/null; \
		done; \
	fi; \
	echo "prepared Orlix kernel port tree: $$port_dir (profile $(PROFILE))"

__prepare-kbuild: __prepare-port
	@set -euo pipefail; \
	linux_make="$(LINUX_MAKE)"; \
	if [ -z "$$linux_make" ]; then linux_make="$$(command -v gmake || true)"; fi; \
	if [ -z "$$linux_make" ]; then \
		echo "GNU Make >= 4.0 is required by Linux Kbuild; install gmake or set LINUX_MAKE=/path/to/gmake" >&2; \
		exit 1; \
	fi; \
	build_dir="$(ORLIX_KERNEL_BUILD_DIR)"; \
	expected_build_dir="$(CURDIR)/Build/OrlixKernel/build/$(PROFILE)"; \
	if [ "$$build_dir" != "$$expected_build_dir" ]; then \
		echo "Orlix kernel build directory must be $$expected_build_dir: $$build_dir" >&2; \
		exit 1; \
	fi; \
	for path in Build/OrlixKernel/build; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked path: $$path" >&2; exit 1; fi; \
	done; \
	linux_sed_dir=""; \
	if [ -n "$(LINUX_SED)" ]; then \
		linux_sed="$(LINUX_SED)"; \
		case "$$linux_sed" in /*) ;; *) linux_sed="$(CURDIR)/$$linux_sed" ;; esac; \
		[ -x "$$linux_sed" ] || { echo "GNU sed is required by Linux Kbuild on this host; LINUX_SED is not executable: $$linux_sed" >&2; exit 1; }; \
		sed_shim_dir="$(CURDIR)/Build/OrlixKernel/tool-shims/$(PROFILE)"; \
		if [ -e Build/OrlixKernel/tool-shims ] && [ -L Build/OrlixKernel/tool-shims ]; then echo "refusing to use symlinked Build/OrlixKernel/tool-shims directory" >&2; exit 1; fi; \
		if [ -e Build/OrlixKernel/tool-shims/$(PROFILE) ] && [ -L Build/OrlixKernel/tool-shims/$(PROFILE) ]; then echo "refusing to use symlinked Build/OrlixKernel/tool-shims/$(PROFILE) directory" >&2; exit 1; fi; \
		mkdir -p "$$sed_shim_dir"; \
		ln -sf "$$linux_sed" "$$sed_shim_dir/sed"; \
		linux_sed_dir="$$sed_shim_dir"; \
	elif [ -x /opt/homebrew/opt/gnu-sed/libexec/gnubin/sed ]; then linux_sed_dir=/opt/homebrew/opt/gnu-sed/libexec/gnubin; fi; \
	if [ -z "$$linux_sed_dir" ]; then \
		echo "GNU sed is required by Linux Kbuild on this host; install gnu-sed or set LINUX_SED=/path/to/gnu/sed" >&2; \
		exit 1; \
	fi; \
	linux_llvm_bin="$(LINUX_LLVM_BIN)"; \
	PATH="$$linux_sed_dir:$${linux_llvm_bin:+$$linux_llvm_bin:}$$PATH"; \
	export PATH; \
	sed --version >/dev/null 2>&1 || { echo "GNU sed is required by Linux Kbuild on this host" >&2; exit 1; }; \
	command -v llvm-ar >/dev/null 2>&1 || { echo "llvm-ar is required by Linux Kbuild; install LLVM or set LINUX_LLVM_BIN=/path/to/llvm/bin" >&2; exit 1; }; \
	rm -rf "$$build_dir"; \
	mkdir -p "$$build_dir"; \
	"$$linux_make" -C "$(ORLIX_KERNEL_PORT_DIR)" O="$$build_dir" ARCH="$(LINUX_ARCH)" LLVM=1 CLANG_TARGET_FLAGS=aarch64-linux-gnu HOSTCFLAGS="-I$(LINUX_HOST_COMPAT_INCLUDE_ROOT) -include linux_arm_elf_compat.h -D_UUID_T" mrproper defconfig prepare scripts dtbs; \
	for dtb in appstore development; do \
		[ -f "$$build_dir/arch/$(LINUX_ARCH)/boot/dts/$$dtb.dtb" ] || { echo "missing profile DTB: $$build_dir/arch/$(LINUX_ARCH)/boot/dts/$$dtb.dtb" >&2; exit 1; }; \
	done; \
	echo "prepared Orlix Kbuild output without a standalone image: $$build_dir (profile $(PROFILE))"

__headers-install: __prepare-kbuild
	@set -euo pipefail; \
	linux_make="$(LINUX_MAKE)"; \
	if [ -z "$$linux_make" ]; then linux_make="$$(command -v gmake || true)"; fi; \
	if [ -z "$$linux_make" ]; then \
		echo "GNU Make >= 4.0 is required by Linux headers_install; install gmake or set LINUX_MAKE=/path/to/gmake" >&2; \
		exit 1; \
	fi; \
	linux_sed_dir=""; \
	if [ -n "$(LINUX_SED)" ]; then \
		linux_sed="$(LINUX_SED)"; \
		case "$$linux_sed" in /*) ;; *) linux_sed="$(CURDIR)/$$linux_sed" ;; esac; \
		[ -x "$$linux_sed" ] || { echo "GNU sed is required by Linux headers_install; LINUX_SED is not executable: $$linux_sed" >&2; exit 1; }; \
		sed_shim_dir="$(CURDIR)/Build/OrlixKernel/tool-shims/$(PROFILE)-headers"; \
		if [ -e Build/OrlixKernel/tool-shims ] && [ -L Build/OrlixKernel/tool-shims ]; then echo "refusing to use symlinked Build/OrlixKernel/tool-shims directory" >&2; exit 1; fi; \
		mkdir -p "$$sed_shim_dir"; \
		ln -sf "$$linux_sed" "$$sed_shim_dir/sed"; \
		linux_sed_dir="$$sed_shim_dir"; \
	elif [ -x /opt/homebrew/opt/gnu-sed/libexec/gnubin/sed ]; then linux_sed_dir=/opt/homebrew/opt/gnu-sed/libexec/gnubin; fi; \
	if [ -z "$$linux_sed_dir" ]; then \
		echo "GNU sed is required by Linux headers_install; install gnu-sed or set LINUX_SED=/path/to/gnu/sed" >&2; \
		exit 1; \
	fi; \
	PATH="$$linux_sed_dir:$$PATH"; \
	export PATH; \
	sed --version >/dev/null 2>&1 || { echo "GNU sed is required by Linux headers_install" >&2; exit 1; }; \
	for path in Build/OrlixMLibC Build/OrlixMLibC/kernel-headers; do \
		if [ -e "$$path" ] && [ -L "$$path" ]; then echo "refusing to use symlinked path: $$path" >&2; exit 1; fi; \
	done; \
	rm -rf "$(ORLIX_MLIBC_KERNEL_HEADERS_DIR)"; \
	mkdir -p "$(ORLIX_MLIBC_KERNEL_HEADERS_DIR)"; \
	"$$linux_make" -C "$(ORLIX_KERNEL_PORT_DIR)" O="$(ORLIX_KERNEL_BUILD_DIR)" ARCH="$(LINUX_ARCH)" LLVM=1 INSTALL_HDR_PATH="$(ORLIX_MLIBC_KERNEL_HEADERS_DIR)" headers_install; \
	[ -d "$(ORLIX_MLIBC_KERNEL_HEADERS_DIR)/include" ] || { echo "missing installed Orlix UAPI headers: $(ORLIX_MLIBC_KERNEL_HEADERS_DIR)/include" >&2; exit 1; }; \
	echo "installed Orlix UAPI headers: $(ORLIX_MLIBC_KERNEL_HEADERS_DIR)/include"

__kunit: __prepare-kbuild
	@set -euo pipefail; \
	linux_make="$(LINUX_MAKE)"; \
	if [ -z "$$linux_make" ]; then linux_make="$$(command -v gmake || true)"; fi; \
	if [ -z "$$linux_make" ]; then \
		echo "GNU Make >= 4.0 is required by Linux KUnit builds; install gmake or set LINUX_MAKE=/path/to/gmake" >&2; \
		exit 1; \
	fi; \
	linux_llvm_bin="$(LINUX_LLVM_BIN)"; \
	linux_sed_dir=""; \
	if [ -n "$(LINUX_SED)" ]; then \
		linux_sed="$(LINUX_SED)"; \
		case "$$linux_sed" in /*) ;; *) linux_sed="$(CURDIR)/$$linux_sed" ;; esac; \
		[ -x "$$linux_sed" ] || { echo "GNU sed is required by Linux KUnit builds; LINUX_SED is not executable: $$linux_sed" >&2; exit 1; }; \
		sed_shim_dir="$(CURDIR)/Build/OrlixKernel/tool-shims/$(PROFILE)-kunit"; \
		if [ -e Build/OrlixKernel/tool-shims ] && [ -L Build/OrlixKernel/tool-shims ]; then echo "refusing to use symlinked Build/OrlixKernel/tool-shims directory" >&2; exit 1; fi; \
		mkdir -p "$$sed_shim_dir"; \
		ln -sf "$$linux_sed" "$$sed_shim_dir/sed"; \
		linux_sed_dir="$$sed_shim_dir"; \
	elif [ -x /opt/homebrew/opt/gnu-sed/libexec/gnubin/sed ]; then linux_sed_dir=/opt/homebrew/opt/gnu-sed/libexec/gnubin; fi; \
	if [ -z "$$linux_sed_dir" ]; then \
		echo "GNU sed is required by Linux KUnit builds; install gnu-sed or set LINUX_SED=/path/to/gnu/sed" >&2; \
		exit 1; \
	fi; \
	coreutils_dir=""; \
	if readlink -e / >/dev/null 2>&1; then coreutils_dir="$$(dirname "$$(command -v readlink)")"; \
	elif [ -x /opt/homebrew/opt/coreutils/libexec/gnubin/readlink ]; then coreutils_dir=/opt/homebrew/opt/coreutils/libexec/gnubin; \
	else echo "GNU readlink is required by Linux KUnit builds; install coreutils" >&2; exit 1; fi; \
	PATH="$$linux_sed_dir:$$coreutils_dir:$${linux_llvm_bin:+$$linux_llvm_bin:}$$PATH"; \
	export PATH; \
	sed --version >/dev/null 2>&1 || { echo "GNU sed is required by Linux KUnit builds" >&2; exit 1; }; \
	rm -rf "$(ORLIX_KUNIT_BUILD_DIR)"; \
	mkdir -p "$(ORLIX_KUNIT_BUILD_DIR)"; \
	"$$linux_make" -C "$(ORLIX_KERNEL_PORT_DIR)" O="$(ORLIX_KUNIT_BUILD_DIR)" ARCH="$(LINUX_ARCH)" LLVM=1 CLANG_TARGET_FLAGS=aarch64-linux-gnu HOSTCFLAGS="-I$(LINUX_HOST_COMPAT_INCLUDE_ROOT) -include linux_arm_elf_compat.h -D_UUID_T" defconfig; \
	"$(ORLIX_KERNEL_PORT_DIR)/scripts/kconfig/merge_config.sh" -m -O "$(ORLIX_KUNIT_BUILD_DIR)" "$(ORLIX_KUNIT_BUILD_DIR)/.config" "$(ORLIX_KERNEL_PORT_DIR)/arch/$(LINUX_ARCH)/.kunitconfig"; \
	"$$linux_make" -C "$(ORLIX_KERNEL_PORT_DIR)" O="$(ORLIX_KUNIT_BUILD_DIR)" ARCH="$(LINUX_ARCH)" LLVM=1 CLANG_TARGET_FLAGS=aarch64-linux-gnu HOSTCFLAGS="-I$(LINUX_HOST_COMPAT_INCLUDE_ROOT) -include linux_arm_elf_compat.h -D_UUID_T" olddefconfig arch/$(LINUX_ARCH)/boot/boot_test.o; \
	echo "built Orlix KUnit objects: $(ORLIX_KUNIT_BUILD_DIR)"

__linux-userspace-sysroot:
	@set -euo pipefail; \
	if [ -L "$(ORLIX_LINUX_USERSPACE_SYSROOT_BOOTSTRAP_DIR)" ]; then \
		echo "refusing to use symlinked Linux userspace sysroot: $(ORLIX_LINUX_USERSPACE_SYSROOT_BOOTSTRAP_DIR)" >&2; \
		exit 1; \
	fi; \
	if [ ! -d "$(ORLIX_LINUX_USERSPACE_SYSROOT_BOOTSTRAP_DIR)" ]; then \
		./scripts/bootstrap-orlix-linux-userspace-sysroot.sh --output "$(ORLIX_LINUX_USERSPACE_SYSROOT_BOOTSTRAP_DIR)"; \
	fi; \
	[ -d "$(ORLIX_LINUX_USERSPACE_SYSROOT_BOOTSTRAP_DIR)" ] || { echo "missing Linux userspace sysroot: $(ORLIX_LINUX_USERSPACE_SYSROOT_BOOTSTRAP_DIR)" >&2; exit 1; }

__kselftest-install: __validate-profile
	@set -euo pipefail; \
	selected_libc="$(libc)"; \
	case "$$selected_libc" in linux|orlixmlibc) ;; *) echo "unsupported libc=$$selected_libc (expected linux or orlixmlibc)" >&2; exit 1 ;; esac; \
	sysroot="$(KSELFTEST_SYSROOT)"; \
	install_dir="$(KSELFTEST_INSTALL_DIR)"; \
	header_flags="$(KSELFTEST_HEADER_FLAGS)"; \
	proof_label="$(KSELFTEST_PROOF_LABEL)"; \
	case "$$sysroot" in /*) ;; *) sysroot="$(CURDIR)/$$sysroot" ;; esac; \
	[ -d "$$sysroot" ] || { echo "missing kselftest sysroot: $$sysroot" >&2; exit 1; }; \
	linux_make="$(LINUX_MAKE)"; \
	if [ -z "$$linux_make" ]; then linux_make="$$(command -v gmake || true)"; fi; \
	if [ -z "$$linux_make" ]; then echo "GNU Make >= 4.0 is required by Linux kselftest; install gmake or set LINUX_MAKE=/path/to/gmake" >&2; exit 1; fi; \
	linux_llvm_bin="$(LINUX_LLVM_BIN)"; \
	coreutils_dir=""; \
	if readlink -e / >/dev/null 2>&1; then coreutils_dir="$$(dirname "$$(command -v readlink)")"; \
	elif [ -x /opt/homebrew/opt/coreutils/libexec/gnubin/readlink ]; then coreutils_dir=/opt/homebrew/opt/coreutils/libexec/gnubin; \
	else echo "GNU readlink is required by kselftest install; install coreutils" >&2; exit 1; fi; \
	PATH="$$coreutils_dir:$${linux_llvm_bin:+$$linux_llvm_bin:}$$PATH"; \
	export PATH; \
	command -v clang >/dev/null 2>&1 || { echo "clang is required to build Linux kselftest artifacts" >&2; exit 1; }; \
	rm -rf "$$install_dir"; \
	mkdir -p "$$(dirname "$$install_dir")"; \
	"$$linux_make" -C "$(ORLIX_KERNEL_PORT_DIR)/tools/testing/selftests" \
		O="$(ORLIX_KERNEL_BUILD_DIR)" \
		TARGETS=orlix \
		KSFT_INSTALL_PATH="$$install_dir" \
		ARCH="$(ORLIX_KSELFTEST_ARCH)" \
		LLVM=1 \
		FORCE_TARGETS=1 \
		USERCFLAGS="--sysroot=$$sysroot $$header_flags" \
		USERLDFLAGS="--sysroot=$$sysroot -static -fuse-ld=lld" \
		install; \
	printf 'proof_lane=%s\n' "$$proof_label" > "$$install_dir/proof_lane.txt"; \
	[ -s "$$install_dir/run_kselftest.sh" ] || { echo "missing installed kselftest runner" >&2; exit 1; }; \
	echo "installed Orlix kselftests: $$install_dir (libc $$selected_libc)"

__kselftest-initramfs: kselftest-install
	@set -euo pipefail; \
	selected_libc="$(libc)"; \
	case "$$selected_libc" in linux|orlixmlibc) ;; *) echo "unsupported libc=$$selected_libc (expected linux or orlixmlibc)" >&2; exit 1 ;; esac; \
	install_dir="$(KSELFTEST_INSTALL_DIR)"; \
	output="$(KSELFTEST_INITRAMFS_DIR)"; \
	proof_label="$(KSELFTEST_PROOF_LABEL)"; \
	case "$$output" in \
		"$(CURDIR)"/Build/OrlixKernel/test-initramfs/*|Build/OrlixKernel/test-initramfs/*|"$(CURDIR)"/Build/OrlixMLibC/test-initramfs/*|Build/OrlixMLibC/test-initramfs/*) ;; \
		*) echo "refusing to write test initramfs outside Build test-initramfs roots: $$output" >&2; exit 1 ;; \
	esac; \
	for path in Build Build/OrlixKernel Build/OrlixMLibC Build/OrlixKernel/test-initramfs Build/OrlixMLibC/test-initramfs "$$output"; do \
		if [ -L "$$path" ]; then echo "refusing to package test initramfs through symlinked path: $$path" >&2; exit 1; fi; \
	done; \
	[ -d "$$install_dir" ] || { echo "missing kselftest install directory: $$install_dir" >&2; exit 1; }; \
	for required in \
		"$$install_dir/run_kselftest.sh" \
		"$$install_dir/kselftest/runner.sh" \
		"$$install_dir/kselftest-list.txt" \
		"$$install_dir/orlix/boot_profile_contract" \
		"$$install_dir/orlix/virtio_mmio_probe_contract"; do \
		[ -s "$$required" ] || { echo "missing non-empty kselftest install input: $$required" >&2; exit 1; }; \
	done; \
	grep -qx 'orlix:boot_profile_contract' "$$install_dir/kselftest-list.txt" || { echo "kselftest install list is missing orlix:boot_profile_contract" >&2; exit 1; }; \
	grep -qx 'orlix:virtio_mmio_probe_contract' "$$install_dir/kselftest-list.txt" || { echo "kselftest install list is missing orlix:virtio_mmio_probe_contract" >&2; exit 1; }; \
	output_parent="$$(dirname "$$output")"; \
	rm -rf "$$output"; \
	mkdir -p "$$output_parent" "$$output/kselftest"; \
	cp -R "$$install_dir/." "$$output/kselftest"; \
	{ \
		printf '%s\n' '#!/bin/sh'; \
		printf '%s\n' 'set -eu'; \
		printf '\n'; \
		printf '%s\n' 'mkdir -p /proc /sys /sys/kernel/debug /dev'; \
		printf '%s\n' 'mount -t proc proc /proc 2>/dev/null || true'; \
		printf '%s\n' 'mount -t sysfs sysfs /sys 2>/dev/null || true'; \
		printf '%s\n' 'mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null || true'; \
		printf '\n'; \
		printf '%s\n' "echo \"ORLIX-PROOF-LANE $$proof_label\""; \
		printf '%s\n' "echo \"ORLIX-PROFILE $(PROFILE)\""; \
		printf '%s\n' "echo \"ORLIX-LINUX-ARCH $(LINUX_ARCH)\""; \
		printf '%s\n' "echo \"ORLIX-LINUX-VERSION $(LINUX_VERSION)\""; \
		printf '\n'; \
		printf '%s\n' 'echo "ORLIX-KUNIT-BEGIN"'; \
		printf '%s\n' 'if [ -d /sys/kernel/debug/kunit ]; then'; \
		printf '%s\n' '    found_kunit_results=0'; \
		printf '%s\n' '    for result in /sys/kernel/debug/kunit/*/results; do'; \
		printf '%s\n' '        if [ -r "$$result" ]; then'; \
		printf '%s\n' '            found_kunit_results=1'; \
		printf '%s\n' '            cat "$$result"'; \
		printf '%s\n' '        fi'; \
		printf '%s\n' '    done'; \
		printf '%s\n' '    if [ "$$found_kunit_results" -eq 0 ]; then'; \
		printf '%s\n' '        dmesg || true'; \
		printf '%s\n' '    fi'; \
		printf '%s\n' 'else'; \
		printf '%s\n' '    dmesg || true'; \
		printf '%s\n' 'fi'; \
		printf '%s\n' 'echo "ORLIX-KUNIT-END"'; \
		printf '\n'; \
		printf '%s\n' 'echo "ORLIX-KSELFTEST-BEGIN"'; \
		printf '%s\n' 'cd /kselftest'; \
		printf '%s\n' 'set +e'; \
		printf '%s\n' './run_kselftest.sh -c orlix'; \
		printf '%s\n' 'kselftest_status="$$?"'; \
		printf '%s\n' 'set -e'; \
		printf '%s\n' 'echo "ORLIX-KSELFTEST-END status=$$kselftest_status"'; \
		printf '%s\n' 'exit "$$kselftest_status"'; \
	} > "$$output/init"; \
	chmod 755 "$$output/init"; \
	printf 'proof_lane=%s\n' "$$proof_label" > "$$output/proof_lane.txt"; \
	printf '%s\n' "$(PROFILE)" > "$$output/selected_profile.txt"; \
	printf '%s\n' "$(LINUX_ARCH)" > "$$output/linux_arch.txt"; \
	printf '%s\n' "$(LINUX_VERSION)" > "$$output/linux_version.txt"; \
	shasum -a 256 "$$output/kselftest/kselftest-list.txt" | awk '{ print $$1 }' > "$$output/kselftest-list.sha256"; \
	{ \
		printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>'; \
		printf '%s\n' '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">'; \
		printf '%s\n' '<plist version="1.0">'; \
		printf '%s\n' '<dict>'; \
		printf '%s\n' '    <key>CFBundleIdentifier</key>'; \
		printf '%s\n' '    <string>org.orlix.OrlixTestInitramfs</string>'; \
		printf '%s\n' '    <key>CFBundleName</key>'; \
		printf '%s\n' '    <string>OrlixTestInitramfs</string>'; \
		printf '%s\n' '    <key>CFBundlePackageType</key>'; \
		printf '%s\n' '    <string>BNDL</string>'; \
		printf '%s\n' '    <key>CFBundleShortVersionString</key>'; \
		printf '%s\n' '    <string>0.1</string>'; \
		printf '%s\n' '    <key>CFBundleVersion</key>'; \
		printf '%s\n' '    <string>1</string>'; \
		printf '%s\n' '    <key>OrlixLinuxArch</key>'; \
		printf '%s\n' '    <string>$(LINUX_ARCH)</string>'; \
		printf '%s\n' '    <key>OrlixLinuxVersion</key>'; \
		printf '%s\n' '    <string>$(LINUX_VERSION)</string>'; \
		printf '%s\n' '    <key>OrlixProfile</key>'; \
		printf '%s\n' '    <string>$(PROFILE)</string>'; \
		printf '%s\n' '    <key>OrlixProofLane</key>'; \
		printf '%s\n' "    <string>$$proof_label</string>"; \
		printf '%s\n' '</dict>'; \
		printf '%s\n' '</plist>'; \
	} > "$$output/Info.plist"; \
	plutil -lint "$$output/Info.plist" >/dev/null; \
	echo "packaged kselftest initramfs: $$output (libc $$selected_libc)"

__ios-simulator-framework: xcodeproj
	@set -euo pipefail; \
	command -v "$(XCODEBUILD_MCP)" >/dev/null 2>&1 || { echo "XcodeBuildMCP is required; install xcodebuildmcp or set XCODEBUILD_MCP=/path/to/xcodebuildmcp" >&2; exit 1; }; \
	selector=(); \
	if [ -n "$(ORLIX_IOS_SIMULATOR_ID)" ]; then \
		selector=(--simulator-id "$(ORLIX_IOS_SIMULATOR_ID)"); \
	else \
		selector=(--simulator-name "$(ORLIX_IOS_SIMULATOR_NAME)" --use-latest-os); \
	fi; \
	"$(XCODEBUILD_MCP)" simulator build \
		--project-path "$(CURDIR)/$(ORLIX_XCODE_PROJECT)" \
		--scheme "OrlixKernel" \
		--configuration "Debug" \
		--derived-data-path "$(ORLIX_IOS_SIMULATOR_DERIVED_DATA)" \
		"$${selector[@]}" \
		--output json; \
	[ -d "$(ORLIX_IOS_SIMULATOR_FRAMEWORK)" ] || { echo "missing simulator framework: $(ORLIX_IOS_SIMULATOR_FRAMEWORK)" >&2; exit 1; }

__ios-simulator-xcframework: __ios-simulator-framework
	@set -euo pipefail; \
	./scripts/package-orlixkernel-simulator-xcframework.sh --framework "$(ORLIX_IOS_SIMULATOR_FRAMEWORK)" --output "$(ORLIX_KERNEL_XCFRAMEWORK)"; \
	./scripts/verify-orlixkernel-simulator-xcframework.sh --xcframework "$(ORLIX_KERNEL_XCFRAMEWORK)" --profile "$(PROFILE)" --linux-version "$(LINUX_VERSION)" --linux-arch "$(LINUX_ARCH)"
