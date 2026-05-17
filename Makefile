SHELL := /bin/bash

LINUX_VERSION ?= 6.12
LINUX_ARCH ?= orlix
LINUX_TAG ?= v$(LINUX_VERSION)
LINUX_REMOTE ?= https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git

PROFILE ?= appstore
ORLIX_PROFILES := appstore development

LINUX_UPSTREAM_DIR ?= Linux/upstream/linux-$(LINUX_VERSION)
LINUX_CLONE_CACHE_DIR ?= .cache/linux-clone
LINUX_CLONE_CACHE_REPO := $(CURDIR)/$(LINUX_CLONE_CACHE_DIR)/linux-$(LINUX_VERSION).git

ORLIX_LINUX_OVERLAY ?= Linux/ports/orlix/overlay
ORLIX_LINUX_PATCH_DIR ?= Linux/ports/orlix/patches
override ORLIX_PROFILE_CONFIG := Linux/ports/orlix/configs/$(PROFILE)_defconfig
ORLIX_KERNEL_HEADER ?= OrlixKernel/include/OrlixKernel.h

ORLIX_KERNEL_PORT_DIR ?= Build/OrlixKernel/linux-$(LINUX_VERSION)-port
ORLIX_KERNEL_BUILD_ROOT := $(CURDIR)/Build/OrlixKernel/build
ORLIX_KERNEL_BUILD_DIR := $(ORLIX_KERNEL_BUILD_ROOT)/$(PROFILE)
ORLIX_KERNEL_VMLINUX := $(ORLIX_KERNEL_BUILD_DIR)/vmlinux
ORLIX_KERNEL_PAYLOAD_DIR := $(CURDIR)/Build/OrlixKernel/xcframework-input/OrlixKernelPayload.bundle
ORLIX_KSELFTEST_INSTALL_DIR := $(CURDIR)/Build/OrlixKernel/test-initramfs/kselftest-install/$(PROFILE)
ORLIX_TEST_INITRAMFS_DIR := $(CURDIR)/Build/OrlixKernel/test-initramfs/OrlixTestInitramfs.bundle
ORLIX_XCODE_PROJECT ?= OrlixSystem.xcodeproj
ORLIX_IOS_SIMULATOR_NAME ?= iPhone 17 Pro
ORLIX_IOS_SIMULATOR_ID ?=
ORLIX_IOS_SIMULATOR_DERIVED_DATA ?= $(CURDIR)/.deriveddata/OrlixSystem-sim
ORLIX_IOS_SIMULATOR_FRAMEWORK := $(ORLIX_IOS_SIMULATOR_DERIVED_DATA)/Build/Products/Debug-iphonesimulator/OrlixKernel.framework
ORLIX_KERNEL_XCFRAMEWORK ?= $(CURDIR)/Build/OrlixKernel/xcframework/OrlixKernel.xcframework
XCODEGEN ?= xcodegen
XCODEBUILD_MCP ?= xcodebuildmcp
ORLIX_LINUX_USERSPACE_SYSROOT ?=
ORLIX_LINUX_USERSPACE_SYSROOT_BOOTSTRAP_DIR ?= Build/OrlixKernel/linux-userspace-sysroot/aarch64
ORLIX_KSELFTEST_ARCH ?= arm64

LINUX_MAKE ?=
LINUX_SED ?=
LINUX_LLVM_BIN ?= $(shell if command -v llvm-ar >/dev/null 2>&1; then dirname "$$(command -v llvm-ar)"; elif [ -x /opt/homebrew/opt/llvm/bin/llvm-ar ]; then printf '%s\n' /opt/homebrew/opt/llvm/bin; fi)
LINUX_HOST_COMPAT_INCLUDE_ROOT := $(CURDIR)/tools/linux_host_compat/include

.PHONY: bootstrap-linux-upstream validate-orlix-profile prepare-orlixkernel-port build-linux-kernel stage-orlixkernel-payload bootstrap-orlix-linux-userspace-sysroot build-orlix-kselftests stage-orlix-test-initramfs generate-xcode-project prepare-ios-packaging build-ios-simulator-framework package-ios-simulator-xcframework verify-ios-simulator-xcframework test-ios-simulator-packaging run-ios-simulator-terminal proof-ios-simulator-packaging

bootstrap-linux-upstream:
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
	if [ -L Build ]; then \
		echo "refusing to use symlinked Build directory" >&2; \
		exit 1; \
	fi; \
	if [ -e Linux ] && [ -L Linux ]; then \
		echo "refusing to use symlinked Linux directory" >&2; \
		exit 1; \
	fi; \
	if [ -e Linux/upstream ] && [ -L Linux/upstream ]; then \
		echo "refusing to use symlinked Linux/upstream directory" >&2; \
		exit 1; \
	fi; \
	if [ -e .cache ] && [ -L .cache ]; then \
		echo "refusing to use symlinked .cache directory" >&2; \
		exit 1; \
	fi; \
	if [ -e .cache/linux-clone ] && [ -L .cache/linux-clone ]; then \
		echo "refusing to use symlinked .cache/linux-clone directory" >&2; \
		exit 1; \
	fi; \
	if [ -L "$$upstream_dir" ]; then \
		echo "refusing to replace symlinked upstream path: $$upstream_dir" >&2; \
		exit 1; \
	fi; \
	if [ -L "$$clone_cache" ]; then \
		echo "refusing to use symlinked Linux clone cache path: $$clone_cache" >&2; \
		exit 1; \
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

validate-orlix-profile:
	@set -euo pipefail; \
	profile="$(PROFILE)"; \
	case " $(ORLIX_PROFILES) " in \
		*" $$profile "*) ;; \
		*) echo "unsupported PROFILE=$$profile (expected one of: $(ORLIX_PROFILES))" >&2; exit 1 ;; \
	esac; \
	config="$(ORLIX_PROFILE_CONFIG)"; \
	[ -f "$$config" ] || { echo "missing profile defconfig: $$config" >&2; exit 1; }

prepare-orlixkernel-port: validate-orlix-profile bootstrap-linux-upstream
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
	if [ -L Build ]; then \
		echo "refusing to use symlinked Build directory" >&2; \
		exit 1; \
	fi; \
	if [ -e Build/OrlixKernel ] && [ -L Build/OrlixKernel ]; then \
		echo "refusing to use symlinked Build/OrlixKernel directory" >&2; \
		exit 1; \
	fi; \
	if [ ! -d "$$overlay_dir" ]; then \
		echo "missing Linux overlay directory: $$overlay_dir" >&2; \
		exit 1; \
	fi; \
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

build-linux-kernel: prepare-orlixkernel-port
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
	vmlinux="$$build_dir/vmlinux"; \
	if [ -e Build/OrlixKernel/build ] && [ -L Build/OrlixKernel/build ]; then \
		echo "refusing to use symlinked Build/OrlixKernel/build directory" >&2; \
		exit 1; \
	fi; \
	linux_sed_dir=""; \
	if [ -n "$(LINUX_SED)" ]; then \
		linux_sed="$(LINUX_SED)"; \
		case "$$linux_sed" in /*) ;; *) linux_sed="$(CURDIR)/$$linux_sed" ;; esac; \
		[ -x "$$linux_sed" ] || { echo "GNU sed is required by Linux Kbuild on this host; LINUX_SED is not executable: $$linux_sed" >&2; exit 1; }; \
		sed_shim_dir="$(CURDIR)/Build/OrlixKernel/tool-shims/$(PROFILE)"; \
		if [ -e Build/OrlixKernel/tool-shims ] && [ -L Build/OrlixKernel/tool-shims ]; then \
			echo "refusing to use symlinked Build/OrlixKernel/tool-shims directory" >&2; \
			exit 1; \
		fi; \
		if [ -e Build/OrlixKernel/tool-shims/$(PROFILE) ] && [ -L Build/OrlixKernel/tool-shims/$(PROFILE) ]; then \
			echo "refusing to use symlinked Build/OrlixKernel/tool-shims/$(PROFILE) directory" >&2; \
			exit 1; \
		fi; \
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
	"$$linux_make" -C "$(ORLIX_KERNEL_PORT_DIR)" O="$$build_dir" ARCH="$(LINUX_ARCH)" LLVM=1 CLANG_TARGET_FLAGS=aarch64-linux-gnu HOSTCFLAGS="-I$(LINUX_HOST_COMPAT_INCLUDE_ROOT) -include linux_arm_elf_compat.h -D_UUID_T" mrproper defconfig vmlinux dtbs; \
	[ -f "$$vmlinux" ] || { echo "missing vmlinux artifact: $$vmlinux" >&2; exit 1; }; \
	for dtb in appstore development; do \
		[ -f "$$build_dir/arch/$(LINUX_ARCH)/boot/dts/$$dtb.dtb" ] || { echo "missing profile DTB: $$build_dir/arch/$(LINUX_ARCH)/boot/dts/$$dtb.dtb" >&2; exit 1; }; \
	done; \
	echo "Linux vmlinux ready: $$vmlinux (profile $(PROFILE))"

stage-orlixkernel-payload: build-linux-kernel
	@set -euo pipefail; \
	./scripts/stage-orlixkernel-payload.sh --profile "$(PROFILE)" --linux-version "$(LINUX_VERSION)" --linux-arch "$(LINUX_ARCH)"; \
	[ -d "$(ORLIX_KERNEL_PAYLOAD_DIR)" ] || { echo "missing staged payload: $(ORLIX_KERNEL_PAYLOAD_DIR)" >&2; exit 1; }

bootstrap-orlix-linux-userspace-sysroot:
	@set -euo pipefail; \
	./scripts/bootstrap-orlix-linux-userspace-sysroot.sh --output "$(ORLIX_LINUX_USERSPACE_SYSROOT_BOOTSTRAP_DIR)"; \
	[ -d "$(ORLIX_LINUX_USERSPACE_SYSROOT_BOOTSTRAP_DIR)" ] || { echo "missing Linux userspace sysroot: $(ORLIX_LINUX_USERSPACE_SYSROOT_BOOTSTRAP_DIR)" >&2; exit 1; }

build-orlix-kselftests: validate-orlix-profile
	@set -euo pipefail; \
	linux_sysroot="$(ORLIX_LINUX_USERSPACE_SYSROOT)"; \
	if [ -z "$$linux_sysroot" ]; then \
		echo "ORLIX_LINUX_USERSPACE_SYSROOT is required to build Linux kselftest artifacts for the test initramfs" >&2; \
		exit 1; \
	fi; \
	case "$$linux_sysroot" in /*) ;; *) linux_sysroot="$(CURDIR)/$$linux_sysroot" ;; esac; \
	[ -d "$$linux_sysroot" ] || { echo "missing Linux userspace sysroot: $$linux_sysroot" >&2; exit 1; }; \
	linux_make="$(LINUX_MAKE)"; \
	if [ -z "$$linux_make" ]; then linux_make="$$(command -v gmake || true)"; fi; \
	if [ -z "$$linux_make" ]; then \
		echo "GNU Make >= 4.0 is required by Linux kselftest; install gmake or set LINUX_MAKE=/path/to/gmake" >&2; \
		exit 1; \
	fi; \
	linux_llvm_bin="$(LINUX_LLVM_BIN)"; \
	coreutils_dir=""; \
	if readlink -e / >/dev/null 2>&1; then coreutils_dir="$$(dirname "$$(command -v readlink)")"; \
	elif [ -x /opt/homebrew/opt/coreutils/libexec/gnubin/readlink ]; then coreutils_dir=/opt/homebrew/opt/coreutils/libexec/gnubin; \
	else echo "GNU readlink is required by kselftest install; install coreutils" >&2; exit 1; fi; \
	PATH="$$coreutils_dir:$${linux_llvm_bin:+$$linux_llvm_bin:}$$PATH"; \
	export PATH; \
	command -v clang >/dev/null 2>&1 || { echo "clang is required to build Linux kselftest artifacts" >&2; exit 1; }; \
	$(MAKE) build-linux-kernel PROFILE="$(PROFILE)"; \
	rm -rf "$(ORLIX_KSELFTEST_INSTALL_DIR)"; \
	mkdir -p "$$(dirname "$(ORLIX_KSELFTEST_INSTALL_DIR)")"; \
	"$$linux_make" -C "$(ORLIX_KERNEL_PORT_DIR)/tools/testing/selftests" \
		O="$(ORLIX_KERNEL_BUILD_DIR)" \
		TARGETS=orlix \
		KSFT_INSTALL_PATH="$(ORLIX_KSELFTEST_INSTALL_DIR)" \
		ARCH="$(ORLIX_KSELFTEST_ARCH)" \
		LLVM=1 \
		FORCE_TARGETS=1 \
		USERCFLAGS="--sysroot=$$linux_sysroot" \
		USERLDFLAGS="--sysroot=$$linux_sysroot -static -fuse-ld=lld" \
		install; \
	[ -s "$(ORLIX_KSELFTEST_INSTALL_DIR)/run_kselftest.sh" ] || { echo "missing installed kselftest runner" >&2; exit 1; }

stage-orlix-test-initramfs: build-orlix-kselftests
	@set -euo pipefail; \
	./scripts/stage-orlix-test-initramfs.sh --profile "$(PROFILE)" --linux-version "$(LINUX_VERSION)" --linux-arch "$(LINUX_ARCH)" --kselftest-install "$(ORLIX_KSELFTEST_INSTALL_DIR)" --output "$(ORLIX_TEST_INITRAMFS_DIR)"; \
	[ -d "$(ORLIX_TEST_INITRAMFS_DIR)" ] || { echo "missing staged test initramfs resource: $(ORLIX_TEST_INITRAMFS_DIR)" >&2; exit 1; }

generate-xcode-project:
	@set -euo pipefail; \
	command -v "$(XCODEGEN)" >/dev/null 2>&1 || { echo "XcodeGen is required; install xcodegen or set XCODEGEN=/path/to/xcodegen" >&2; exit 1; }; \
	"$(XCODEGEN)" generate --spec project.yml

prepare-ios-packaging: stage-orlixkernel-payload
	@set -euo pipefail; \
	command -v "$(XCODEGEN)" >/dev/null 2>&1 || { echo "XcodeGen is required; install xcodegen or set XCODEGEN=/path/to/xcodegen" >&2; exit 1; }; \
	"$(XCODEGEN)" generate --spec project.yml; \
	echo "iOS packaging inputs ready: project.yml generated $(ORLIX_XCODE_PROJECT) and staged $(ORLIX_KERNEL_PAYLOAD_DIR)"

build-ios-simulator-framework: prepare-ios-packaging
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

package-ios-simulator-xcframework: build-ios-simulator-framework
	@set -euo pipefail; \
	./scripts/package-orlixkernel-simulator-xcframework.sh --framework "$(ORLIX_IOS_SIMULATOR_FRAMEWORK)" --output "$(ORLIX_KERNEL_XCFRAMEWORK)"; \
	[ -d "$(ORLIX_KERNEL_XCFRAMEWORK)" ] || { echo "missing simulator XCFramework: $(ORLIX_KERNEL_XCFRAMEWORK)" >&2; exit 1; }

verify-ios-simulator-xcframework: package-ios-simulator-xcframework
	@set -euo pipefail; \
	./scripts/verify-orlixkernel-simulator-xcframework.sh --xcframework "$(ORLIX_KERNEL_XCFRAMEWORK)" --profile "$(PROFILE)" --linux-version "$(LINUX_VERSION)" --linux-arch "$(LINUX_ARCH)"

test-ios-simulator-packaging: prepare-ios-packaging
	@set -euo pipefail; \
	command -v "$(XCODEBUILD_MCP)" >/dev/null 2>&1 || { echo "XcodeBuildMCP is required; install xcodebuildmcp or set XCODEBUILD_MCP=/path/to/xcodebuildmcp" >&2; exit 1; }; \
	selector=(); \
	if [ -n "$(ORLIX_IOS_SIMULATOR_ID)" ]; then \
		selector=(--simulator-id "$(ORLIX_IOS_SIMULATOR_ID)"); \
	else \
		selector=(--simulator-name "$(ORLIX_IOS_SIMULATOR_NAME)" --use-latest-os); \
	fi; \
	"$(XCODEBUILD_MCP)" simulator test \
		--project-path "$(CURDIR)/$(ORLIX_XCODE_PROJECT)" \
		--scheme "OrlixKernelPackaging" \
		--configuration "Debug" \
		--derived-data-path "$(ORLIX_IOS_SIMULATOR_DERIVED_DATA)" \
		"$${selector[@]}" \
		--output json

run-ios-simulator-terminal: prepare-ios-packaging
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

proof-ios-simulator-packaging: verify-ios-simulator-xcframework test-ios-simulator-packaging run-ios-simulator-terminal
