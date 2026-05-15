SHELL := /bin/bash

LINUX_VERSION ?= 6.12
LINUX_ARCH ?= orlix
LINUX_TAG ?= v$(LINUX_VERSION)
LINUX_REMOTE ?= https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git

LINUX_UPSTREAM_DIR ?= Linux/upstream/linux-$(LINUX_VERSION)
LINUX_WORK_DIR ?= Build/linux-work
LINUX_CLONE_CACHE_DIR ?= .cache/linux-clone
LINUX_CLONE_CACHE_REPO := $(CURDIR)/$(LINUX_CLONE_CACHE_DIR)/linux-$(LINUX_VERSION).git

ORLIX_LINUX_OVERLAY ?= Linux/ports/orlix/overlay
ORLIX_LINUX_PATCH_DIR ?= Linux/ports/orlix/patches
ORLIX_KERNEL_HEADER ?= OrlixKernel/include/OrlixKernel.h
ORLIX_XCFRAMEWORK_DIR ?= Build/OrlixKernel.xcframework

LINUX_MAKE ?=
LINUX_SED ?=
LINUX_LLVM_BIN ?= $(shell if command -v llvm-ar >/dev/null 2>&1; then dirname "$$(command -v llvm-ar)"; elif [ -x /opt/homebrew/opt/llvm/bin/llvm-ar ]; then printf '%s\n' /opt/homebrew/opt/llvm/bin; fi)
LINUX_HOST_COMPAT_INCLUDE_ROOT := $(CURDIR)/tools/linux_host_compat/include

ORLIX_KBUILD_SIM_DIR := $(CURDIR)/Build/linux-orlix-kernel-simulator
ORLIX_SIMULATOR_DIR := $(CURDIR)/Build/linux-simulator
ORLIX_IPHONEOS_DIR := $(CURDIR)/Build/linux-iphoneos
ORLIX_BOOT_CONTRACT_DIR := $(CURDIR)/Build/bootloader-contract

.PHONY: bootstrap-linux-upstream prepare-linux-worktree build-linux-orlix-kernel-simulator build-linux-simulator build-linux-iphoneos build-static-library package-orlixkernel-xcframework test-bootloader-contract

bootstrap-linux-upstream:
	@set -euo pipefail; \
	linux_remote="$(LINUX_REMOTE)"; \
	linux_tag="$(LINUX_TAG)"; \
	upstream_dir="$(LINUX_UPSTREAM_DIR)"; \
	clone_cache="$(LINUX_CLONE_CACHE_REPO)"; \
	if [ -L Build ]; then \
		echo "refusing to use symlinked Build directory" >&2; \
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

prepare-linux-worktree: bootstrap-linux-upstream
	@set -euo pipefail; \
	upstream_dir="$(LINUX_UPSTREAM_DIR)"; \
	work_dir="$(LINUX_WORK_DIR)"; \
	overlay_dir="$(ORLIX_LINUX_OVERLAY)"; \
	patch_dir="$(ORLIX_LINUX_PATCH_DIR)"; \
	exception_dir="$$patch_dir/exceptions"; \
	forbidden_re='^(fs|kernel|mm|ipc|net|include/linux|include/uapi)(/|$$)'; \
	if [ "$$work_dir" != "Build/linux-work" ]; then \
		echo "Linux work directory must be Build/linux-work: $$work_dir" >&2; \
		exit 1; \
	fi; \
	if [ "$$work_dir" = "$$upstream_dir" ]; then \
		echo "Linux work directory must not equal upstream directory: $$work_dir" >&2; \
		exit 1; \
	fi; \
	if [ -L Build ]; then \
		echo "refusing to use symlinked Build directory" >&2; \
		exit 1; \
	fi; \
	if [ ! -d "$$overlay_dir" ]; then \
		echo "missing Linux overlay directory: $$overlay_dir" >&2; \
		exit 1; \
	fi; \
	rm -rf "$$work_dir"; \
	mkdir -p "$$work_dir"; \
	git -C "$$upstream_dir" archive --format=tar HEAD | tar -x -C "$$work_dir"; \
	cp -R "$$overlay_dir/." "$$work_dir"; \
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
			patch -d "$$work_dir" -p1 < "$$patch_abs" >/dev/null; \
		done; \
	fi; \
	echo "prepared Linux worktree: $$work_dir"

build-linux-orlix-kernel-simulator: prepare-linux-worktree
	@set -euo pipefail; \
	linux_make="$(LINUX_MAKE)"; \
	if [ -z "$$linux_make" ]; then linux_make="$$(command -v gmake || true)"; fi; \
	if [ -z "$$linux_make" ]; then \
		echo "GNU Make >= 4.0 is required by Linux Kbuild; install gmake or set LINUX_MAKE=/path/to/gmake" >&2; \
		exit 1; \
	fi; \
	linux_sed_dir=""; \
	if [ -n "$(LINUX_SED)" ]; then linux_sed_dir="$$(dirname "$(LINUX_SED)")"; \
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
	build_dir="$(ORLIX_KBUILD_SIM_DIR)"; \
	rm -rf "$$build_dir"; \
	mkdir -p "$$build_dir"; \
	echo "Kbuild boundary: compiling real arch/orlix/kernel objects; vmlinux is intentionally out of scope."; \
	"$$linux_make" -C "$(LINUX_WORK_DIR)" O="$$build_dir" ARCH="$(LINUX_ARCH)" LLVM=1 CLANG_TARGET_FLAGS=aarch64-linux-gnu HOSTCFLAGS="-I$(LINUX_HOST_COMPAT_INCLUDE_ROOT) -include linux_arm_elf_compat.h -D_UUID_T" mrproper defconfig arch/orlix/kernel/; \
	echo "arch/orlix kernel object compile boundary reached: Build/linux-orlix-kernel-simulator"

build-linux-simulator: prepare-linux-worktree
	@$(MAKE) build-static-library SDK=iphonesimulator MIN_VERSION=-mios-simulator-version-min=16.0 OUTPUT_DIR="$(ORLIX_SIMULATOR_DIR)"

build-linux-iphoneos: prepare-linux-worktree
	@$(MAKE) build-static-library SDK=iphoneos MIN_VERSION=-miphoneos-version-min=16.0 OUTPUT_DIR="$(ORLIX_IPHONEOS_DIR)"

build-static-library:
	@set -euo pipefail; \
	linux_make="$(LINUX_MAKE)"; \
	if [ -z "$$linux_make" ]; then linux_make="$$(command -v gmake || true)"; fi; \
	if [ -z "$$linux_make" ]; then \
		echo "GNU Make >= 4.0 is required by Linux Kbuild; install gmake or set LINUX_MAKE=/path/to/gmake" >&2; \
		exit 1; \
	fi; \
	linux_llvm_bin="$(LINUX_LLVM_BIN)"; \
	PATH="$${linux_llvm_bin:+$$linux_llvm_bin:}$$PATH"; \
	export PATH; \
	command -v llvm-ar >/dev/null 2>&1 || { echo "llvm-ar is required by Linux Kbuild; install LLVM or set LINUX_LLVM_BIN=/path/to/llvm/bin" >&2; exit 1; }; \
	build_dir="$(OUTPUT_DIR)"; \
	objects_dir="$$build_dir/objects"; \
	mkdir -p "$$build_dir"; \
	"$$linux_make" -C "$(LINUX_WORK_DIR)" O="$$build_dir" ARCH="$(LINUX_ARCH)" LLVM=1 mrproper defconfig; \
	rm -rf "$$objects_dir"; \
	mkdir -p "$$objects_dir"; \
	for src in boot/*.c; do \
		obj="$$objects_dir/$$(basename "$$src" .c).o"; \
		xcrun --sdk "$(SDK)" clang -arch arm64 "$(MIN_VERSION)" -IOrlixKernel/include -c "$$src" -o "$$obj"; \
	done; \
	xcrun --sdk "$(SDK)" clang -arch arm64 "$(MIN_VERSION)" -IOrlixKernel/include -ILinux/ports/orlix/overlay/arch/orlix/include -c Linux/ports/orlix/overlay/arch/orlix/boot/boot.c -o "$$objects_dir/arch_boot.o"; \
	xcrun --sdk "$(SDK)" libtool -static -o "$$build_dir/libOrlixKernel.a" "$$objects_dir"/*.o; \
	echo "$(SDK) static library ready: $$build_dir/libOrlixKernel.a"

package-orlixkernel-xcframework: build-linux-simulator build-linux-iphoneos
	@set -euo pipefail; \
	xcframework_dir="$(ORLIX_XCFRAMEWORK_DIR)"; \
	header="$(ORLIX_KERNEL_HEADER)"; \
	device_lib="$(ORLIX_IPHONEOS_DIR)/libOrlixKernel.a"; \
	simulator_lib="$(ORLIX_SIMULATOR_DIR)/libOrlixKernel.a"; \
	[ -f "$$header" ] || { echo "missing OrlixKernel header: $$header" >&2; exit 1; }; \
	[ -f "$$device_lib" ] || { echo "missing device library: $$device_lib" >&2; exit 1; }; \
	[ -f "$$simulator_lib" ] || { echo "missing simulator library: $$simulator_lib" >&2; exit 1; }; \
	rm -rf "$$xcframework_dir"; \
	xcodebuild -create-xcframework \
		-library "$$device_lib" -headers OrlixKernel/include \
		-library "$$simulator_lib" -headers OrlixKernel/include \
		-output "$$xcframework_dir" >/dev/null; \
	echo "OrlixKernel.xcframework ready: $$xcframework_dir"

test-bootloader-contract:
	@set -euo pipefail; \
	build_dir="$(ORLIX_BOOT_CONTRACT_DIR)"; \
	mkdir -p "$$build_dir"; \
	$(CC) -std=c11 -Wall -Wextra -Werror \
		-DORLIX_BOOT_TESTING=1 \
		-IOrlixKernel/include \
		-ILinux/ports/orlix/overlay/arch/orlix/include \
		boot/dtb.c \
		boot/image.c \
		boot/initrd.c \
		boot/loader.c \
		boot/params.c \
		boot/rootfs.c \
		Linux/ports/orlix/overlay/arch/orlix/boot/boot.c \
		tests/bootloader_contract.c \
		-o "$$build_dir/bootloader_contract"; \
	"$$build_dir/bootloader_contract"
