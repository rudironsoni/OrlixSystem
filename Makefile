SHELL := /bin/bash

LINUX_VERSION ?= 6.12
LINUX_ARCH ?= arm64
LINUX_TAG ?= v$(LINUX_VERSION)
LINUX_REMOTE ?= https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git
LINUX_UPSTREAM_DIR ?= Linux/upstream/linux-$(LINUX_VERSION)
LINUX_WORK_DIR ?= Build/linux-work
LINUX_LINT_BUILD_DIR ?= Build/linux-lint
ORLIX_LINUX_OVERLAY ?= Linux/ports/orlix/overlay
ORLIX_LINUX_PATCH_DIR ?= Linux/ports/orlix/patches
ORLIX_XCFRAMEWORK_DIR ?= Build/OrlixKernel.xcframework
ORLIX_KERNEL_HEADER ?= OrlixKernel/include/OrlixKernel.h
LINUX_MAKE ?=
LINUX_LLVM_BIN ?=
LINUX_SED ?=
LLVM_PREFIX ?= /opt/homebrew/opt/llvm
CLANG_TIDY ?= $(LLVM_PREFIX)/bin/clang-tidy
LLVM_CONFIG ?= $(LLVM_PREFIX)/bin/llvm-config
CLANG_TIDY_BUILD_DIR ?= .clang-tidy-build
CLANG_TIDY_PLUGIN_SO := $(CLANG_TIDY_BUILD_DIR)/OrlixTidyModule.so
CLANG_TIDY_PLUGIN_DYLIB := $(CLANG_TIDY_BUILD_DIR)/OrlixTidyModule.dylib
IPHONESIM_SDK ?= $(shell xcrun --sdk iphonesimulator --show-sdk-path)
IPHONESIM_PLATFORM_DIR ?= $(shell xcrun --sdk iphonesimulator --show-sdk-platform-path)
IPHONESIM_FRAMEWORK_DIR := $(IPHONESIM_PLATFORM_DIR)/Developer/Library/Frameworks
IPHONESIM_SDK_FRAMEWORK_DIR := $(IPHONESIM_SDK)/Developer/Library/Frameworks
LINUX_KERNEL_GENERATED_INCLUDE_ROOT := $(CURDIR)/$(LINUX_LINT_BUILD_DIR)/include
LINUX_KERNEL_GENERATED_ARCH_UAPI_INCLUDE_ROOT := $(CURDIR)/$(LINUX_LINT_BUILD_DIR)/arch/$(LINUX_ARCH)/include/generated/uapi
LINUX_KERNEL_TOOLS_GENERATED_INCLUDE_ROOT := $(CURDIR)/$(LINUX_WORK_DIR)/tools/include/generated
LINUX_KERNEL_KHEADERS_INCLUDE_ROOT := $(CURDIR)/$(LINUX_WORK_DIR)/include
LINUX_KERNEL_UAPI_INCLUDE_ROOT := $(LINUX_KERNEL_KHEADERS_INCLUDE_ROOT)/uapi
LINUX_KERNEL_ARCH_INCLUDE_ROOT := $(CURDIR)/$(LINUX_WORK_DIR)/arch/$(LINUX_ARCH)/include
LINUX_KERNEL_ARCH_UAPI_INCLUDE_ROOT := $(CURDIR)/$(LINUX_WORK_DIR)/arch/$(LINUX_ARCH)/include/uapi
LINUX_HOST_COMPAT_INCLUDE_ROOT := $(CURDIR)/tools/linux_host_compat/include
ORLIX_LINT_HOST_COMMON_FLAGS := \
	-target arm64-apple-ios26.4-simulator \
	-isysroot $(IPHONESIM_SDK) \
	-mios-simulator-version-min=26.4 \
	-U_FORTIFY_SOURCE \
	-D_FORTIFY_SOURCE=0 \
	-fno-modules
ORLIX_LINT_LINUX_COMMON_FLAGS := \
	-target aarch64-unknown-linux-gnu \
	-ffreestanding \
	-fno-modules
ORLIX_LINT_LINUX_HEADER_FLAGS := \
	-I$(LINUX_KERNEL_GENERATED_INCLUDE_ROOT) \
	-I$(LINUX_KERNEL_GENERATED_ARCH_UAPI_INCLUDE_ROOT) \
	-I$(LINUX_KERNEL_TOOLS_GENERATED_INCLUDE_ROOT) \
	-I$(LINUX_KERNEL_KHEADERS_INCLUDE_ROOT) \
	-I$(LINUX_KERNEL_UAPI_INCLUDE_ROOT) \
	-I$(LINUX_KERNEL_ARCH_INCLUDE_ROOT) \
	-I$(LINUX_KERNEL_ARCH_UAPI_INCLUDE_ROOT)
ORLIX_LINT_C_FLAGS := \
	$(ORLIX_LINT_LINUX_COMMON_FLAGS) \
	-nostdinc \
	-include $(LINUX_KERNEL_KHEADERS_INCLUDE_ROOT)/linux/kconfig.h \
	-include $(LINUX_KERNEL_KHEADERS_INCLUDE_ROOT)/linux/types.h \
	-include $(LINUX_KERNEL_KHEADERS_INCLUDE_ROOT)/linux/stddef.h \
	-include $(LINUX_KERNEL_KHEADERS_INCLUDE_ROOT)/linux/bits.h \
	-isystem $(LLVM_PREFIX)/lib/clang/22/include \
	-I$(CURDIR)/OrlixKernel \
	-I$(CURDIR)/OrlixKernel/include \
	-I$(CURDIR)/OrlixKernel/internal \
	$(ORLIX_LINT_LINUX_HEADER_FLAGS) \
	-D__KERNEL__ \
	-D_XOPEN_SOURCE \
	-fvisibility=hidden
ORLIX_LINT_HOST_C_FLAGS := \
	$(ORLIX_LINT_HOST_COMMON_FLAGS) \
	-I$(CURDIR)/OrlixKernel \
	-I$(CURDIR)/OrlixKernel/include \
	-I$(CURDIR)/OrlixKernel/internal \
	-D_XOPEN_SOURCE \
	-D_DARWIN_C_SOURCE \
	-D_POSIX_C_SOURCE=200112L \
	-fvisibility=hidden
ORLIX_LINT_HOST_KERNEL_C_FLAGS := \
	$(ORLIX_LINT_HOST_COMMON_FLAGS) \
	-I$(CURDIR)/OrlixKernel \
	-I$(CURDIR)/OrlixKernel/include \
	-I$(CURDIR)/OrlixKernel/internal \
	-D_XOPEN_SOURCE \
	-D_DARWIN_C_SOURCE \
	-D_POSIX_C_SOURCE=200112L \
	-fvisibility=hidden \
	-D__KERNEL__ \
	-include $(LINUX_KERNEL_KHEADERS_INCLUDE_ROOT)/linux/kconfig.h \
	$(ORLIX_LINT_LINUX_HEADER_FLAGS)
ORLIX_LINT_KERNEL_TEST_STRICT_C_FLAGS := \
	$(ORLIX_LINT_HOST_KERNEL_C_FLAGS) \
	-include $(LINUX_HOST_COMPAT_INCLUDE_ROOT)/macho_compat.h \
	$(ORLIX_LINT_LINUX_HEADER_FLAGS)
ORLIX_LINT_KERNEL_TEST_C_FLAGS := \
	$(ORLIX_LINT_LINUX_COMMON_FLAGS) \
	-nostdinc \
	-D__EXPORTED_HEADERS__ \
	-isystem $(LLVM_PREFIX)/lib/clang/22/include \
	-I$(LINUX_KERNEL_UAPI_INCLUDE_ROOT) \
	-I$(LINUX_KERNEL_KHEADERS_INCLUDE_ROOT)
ORLIX_LINT_OBJC_FLAGS := \
	$(ORLIX_LINT_HOST_COMMON_FLAGS) \
	-fobjc-arc \
	-I$(CURDIR)/OrlixKernel \
	-I$(CURDIR)/OrlixKernel/include \
	-I$(CURDIR)/OrlixKernel/internal \
	-F$(IPHONESIM_FRAMEWORK_DIR) \
	-F$(IPHONESIM_SDK_FRAMEWORK_DIR) \
	-DDEBUG=1

.PHONY: build-orlix-clang-tidy-module lint lint-linux-surface bootstrap-linux-upstream prepare-linux-worktree prepare-linux-lint-config build-linux-simulator build-linux-iphoneos package-orlixkernel-xcframework

bootstrap-linux-upstream:
	@set -euo pipefail; \
	linux_remote="$(LINUX_REMOTE)"; \
	linux_tag="$(LINUX_TAG)"; \
	linux_upstream_dir="$(LINUX_UPSTREAM_DIR)"; \
	mkdir -p "$$(dirname "$$linux_upstream_dir")"; \
	if [ -d "$$linux_upstream_dir/.git" ]; then \
		git -C "$$linux_upstream_dir" remote set-url origin "$$linux_remote"; \
		git -C "$$linux_upstream_dir" fetch --tags --force origin "$$linux_tag"; \
	else \
		if [ -e "$$linux_upstream_dir" ]; then \
			echo "refusing to replace non-git upstream path: $$linux_upstream_dir" >&2; \
			exit 1; \
		fi; \
		git clone --no-checkout --origin origin "$$linux_remote" "$$linux_upstream_dir"; \
		git -C "$$linux_upstream_dir" fetch --tags --force origin "$$linux_tag"; \
	fi; \
	git -C "$$linux_upstream_dir" -c advice.detachedHead=false checkout --quiet --force --detach "$$linux_tag"; \
	checked_tag="$$(git -C "$$linux_upstream_dir" describe --tags --exact-match HEAD)"; \
	checked_commit="$$(git -C "$$linux_upstream_dir" rev-parse HEAD)"; \
	tag_commit="$$(git -C "$$linux_upstream_dir" rev-list -n1 "$$linux_tag")"; \
	if [ "$$checked_tag" != "$$linux_tag" ] || [ "$$checked_commit" != "$$tag_commit" ]; then \
		echo "expected $$linux_tag at $$tag_commit but checked out $$checked_tag at $$checked_commit" >&2; \
		exit 1; \
	fi; \
	echo "upstream Linux ready: $$linux_upstream_dir ($$checked_tag)"

prepare-linux-worktree: bootstrap-linux-upstream
	@set -euo pipefail; \
	linux_upstream_dir="$(LINUX_UPSTREAM_DIR)"; \
	linux_work_dir="$(LINUX_WORK_DIR)"; \
	overlay_dir="$(ORLIX_LINUX_OVERLAY)"; \
	patch_dir="$(ORLIX_LINUX_PATCH_DIR)"; \
	exception_dir="$$patch_dir/exceptions"; \
	forbidden_re='^(fs|kernel|mm|ipc|net|include/linux|include/uapi)(/|$$)'; \
	if [ "$$linux_work_dir" != "Build/linux-work" ]; then \
		echo "Linux work directory must be Build/linux-work: $$linux_work_dir" >&2; \
		exit 1; \
	fi; \
	if [ -L Build ]; then \
		echo "refusing to use symlinked Build directory" >&2; \
		exit 1; \
	fi; \
	mkdir -p Build; \
	if [ "$$linux_work_dir" = "$$linux_upstream_dir" ]; then \
		echo "Linux work directory must not equal upstream directory: $$linux_work_dir" >&2; \
		exit 1; \
	fi; \
	if [ ! -d "$$overlay_dir" ]; then \
		echo "missing Linux overlay directory: $$overlay_dir" >&2; \
		echo "Create $$overlay_dir before running prepare-linux-worktree." >&2; \
		exit 1; \
	fi; \
	rm -rf "$$linux_work_dir"; \
	mkdir -p "$$linux_work_dir"; \
	git -C "$$linux_upstream_dir" archive --format=tar HEAD | tar -x -C "$$linux_work_dir"; \
	cp -R "$$overlay_dir/." "$$linux_work_dir"; \
	if [ -d "$$patch_dir" ]; then \
		for patch in "$$patch_dir"/*.patch "$$patch_dir"/*.diff; do \
			[ -e "$$patch" ] || continue; \
			patch_name="$$(basename "$$patch")"; \
			case "$$patch" in /*) patch_abs="$$patch" ;; *) patch_abs="$(CURDIR)/$$patch" ;; esac; \
			if awk -v re="$$forbidden_re" '/^\+\+\+ b\// { path = substr($$2, 3); if (path ~ re) { bad = 1 } } END { exit bad ? 0 : 1 }' "$$patch_abs"; then \
				if [ ! -f "$$exception_dir/$$patch_name.md" ]; then \
					echo "patch $$patch_name touches a forbidden upstream path; add $$exception_dir/$$patch_name.md" >&2; \
					exit 1; \
				fi; \
			fi; \
			patch -d "$$linux_work_dir" -p1 < "$$patch_abs" >/dev/null; \
		done; \
	fi; \
	echo "prepared Linux worktree: $$linux_work_dir"

prepare-linux-lint-config: prepare-linux-worktree
	@set -euo pipefail; \
	linux_make="$(LINUX_MAKE)"; \
	if [ -z "$$linux_make" ]; then \
		linux_make="$$(command -v gmake || true)"; \
	fi; \
	if [ -z "$$linux_make" ]; then \
		echo "GNU Make >= 4.0 is required by Linux Kbuild; install gmake or set LINUX_MAKE=/path/to/gmake" >&2; \
		exit 1; \
	fi; \
	linux_sed_dir=""; \
	if [ -n "$(LINUX_SED)" ]; then \
		case "$$(basename "$(LINUX_SED)")" in sed) linux_sed_dir="$$(dirname "$(LINUX_SED)")" ;; esac; \
	fi; \
	if [ -z "$$linux_sed_dir" ] && [ -x /opt/homebrew/opt/gnu-sed/libexec/gnubin/sed ]; then \
		linux_sed_dir=/opt/homebrew/opt/gnu-sed/libexec/gnubin; \
	fi; \
	if [ -z "$$linux_sed_dir" ]; then \
		echo "GNU sed is required by Linux headers; install gnu-sed or set LINUX_SED=/path/to/gnu/sed" >&2; \
		exit 1; \
	fi; \
	PATH="$$linux_sed_dir:$$PATH"; \
	export PATH; \
	sed --version >/dev/null 2>&1 || { echo "GNU sed is required by Linux headers" >&2; exit 1; }; \
	build_dir="$(CURDIR)/$(LINUX_LINT_BUILD_DIR)"; \
	if [ "$(LINUX_LINT_BUILD_DIR)" != "Build/linux-lint" ]; then \
		echo "Linux lint build directory must be Build/linux-lint: $(LINUX_LINT_BUILD_DIR)" >&2; \
		exit 1; \
	fi; \
	rm -rf "$$build_dir"; \
	mkdir -p "$$build_dir"; \
	"$$linux_make" -C "$(LINUX_WORK_DIR)" O="$$build_dir" ARCH="$(LINUX_ARCH)" LLVM=1 defconfig headers; \
	echo "Linux lint configuration ready: $(LINUX_LINT_BUILD_DIR)"

build-linux-simulator: prepare-linux-worktree
	@set -euo pipefail; \
	linux_make="$(LINUX_MAKE)"; \
	if [ -z "$$linux_make" ]; then \
		linux_make="$$(command -v gmake || true)"; \
	fi; \
	if [ -z "$$linux_make" ]; then \
		echo "GNU Make >= 4.0 is required by Linux Kbuild; install gmake or set LINUX_MAKE=/path/to/gmake" >&2; \
		exit 1; \
	fi; \
	build_dir="$(CURDIR)/Build/linux-simulator"; \
	objects_dir="$$build_dir/objects"; \
	mkdir -p "$$build_dir"; \
	"$$linux_make" -C "$(LINUX_WORK_DIR)" O="$$build_dir" ARCH=orlix LLVM=1 mrproper defconfig; \
	rm -rf "$$objects_dir"; \
	mkdir -p "$$objects_dir"; \
	for src in boot/*.c; do \
		obj="$$objects_dir/$$(basename "$$src" .c).o"; \
		xcrun --sdk iphonesimulator clang -arch arm64 -mios-simulator-version-min=16.0 -IOrlixKernel/include -c "$$src" -o "$$obj"; \
	done; \
	xcrun --sdk iphonesimulator libtool -static -o "$$build_dir/libOrlixKernel.a" "$$objects_dir"/*.o; \
	echo "simulator Linux configuration ready: Build/linux-simulator"

build-linux-iphoneos: prepare-linux-worktree
	@set -euo pipefail; \
	linux_make="$(LINUX_MAKE)"; \
	if [ -z "$$linux_make" ]; then \
		linux_make="$$(command -v gmake || true)"; \
	fi; \
	if [ -z "$$linux_make" ]; then \
		echo "GNU Make >= 4.0 is required by Linux Kbuild; install gmake or set LINUX_MAKE=/path/to/gmake" >&2; \
		exit 1; \
	fi; \
	build_dir="$(CURDIR)/Build/linux-iphoneos"; \
	objects_dir="$$build_dir/objects"; \
	mkdir -p "$$build_dir"; \
	"$$linux_make" -C "$(LINUX_WORK_DIR)" O="$$build_dir" ARCH=orlix LLVM=1 mrproper defconfig; \
	rm -rf "$$objects_dir"; \
	mkdir -p "$$objects_dir"; \
	for src in boot/*.c; do \
		obj="$$objects_dir/$$(basename "$$src" .c).o"; \
		xcrun --sdk iphoneos clang -arch arm64 -miphoneos-version-min=16.0 -IOrlixKernel/include -c "$$src" -o "$$obj"; \
	done; \
	xcrun --sdk iphoneos libtool -static -o "$$build_dir/libOrlixKernel.a" "$$objects_dir"/*.o; \
	echo "iphoneos Linux configuration ready: Build/linux-iphoneos"

package-orlixkernel-xcframework: build-linux-simulator build-linux-iphoneos
	@set -euo pipefail; \
	xcframework_dir="$(ORLIX_XCFRAMEWORK_DIR)"; \
	header="$(ORLIX_KERNEL_HEADER)"; \
	if [ ! -f "$$header" ]; then \
		echo "missing OrlixKernel header: $$header" >&2; \
		exit 1; \
	fi; \
	if [ ! -f Build/linux-iphoneos/libOrlixKernel.a ]; then \
		echo "missing device library: Build/linux-iphoneos/libOrlixKernel.a" >&2; \
		exit 1; \
	fi; \
	if [ ! -f Build/linux-simulator/libOrlixKernel.a ]; then \
		echo "missing simulator library: Build/linux-simulator/libOrlixKernel.a" >&2; \
		exit 1; \
	fi; \
	rm -rf "$$xcframework_dir"; \
	xcodebuild -create-xcframework \
		-library Build/linux-iphoneos/libOrlixKernel.a -headers OrlixKernel/include \
		-library Build/linux-simulator/libOrlixKernel.a -headers OrlixKernel/include \
		-output "$$xcframework_dir" >/dev/null; \
	echo "OrlixKernel.xcframework skeleton ready: Build/OrlixKernel.xcframework"

build-orlix-clang-tidy-module:
	@set -euo pipefail; \
	if [ ! -x "$(LLVM_CONFIG)" ]; then \
		echo "llvm-config not found at $(LLVM_CONFIG)" >&2; \
		exit 1; \
	fi; \
	rm -rf "$(CLANG_TIDY_BUILD_DIR)"; \
	mkdir -p "$(CLANG_TIDY_BUILD_DIR)"; \
	cmake -S tools/clang_tidy_orlix -B "$(CLANG_TIDY_BUILD_DIR)" \
		-DCMAKE_BUILD_TYPE=Release \
		-DLLVM_DIR="$$(dirname "$$($(LLVM_CONFIG) --cmakedir)")/llvm" \
		-DClang_DIR="$$(dirname "$$($(LLVM_CONFIG) --cmakedir)")/clang" \
		-DLLVM_CONFIG_EXECUTABLE="$(LLVM_CONFIG)" >/dev/null; \
	cmake --build "$(CLANG_TIDY_BUILD_DIR)" >/dev/null

lint: prepare-linux-lint-config build-orlix-clang-tidy-module
	@set -euo pipefail; \
	plugin_path="$(CLANG_TIDY_PLUGIN_SO)"; \
	if [ ! -f "$$plugin_path" ]; then \
		plugin_path="$(CLANG_TIDY_PLUGIN_DYLIB)"; \
	fi; \
	if [ ! -f "$$plugin_path" ]; then \
		echo "clang-tidy plugin not found" >&2; \
		exit 1; \
	fi; \
	lint_roots="boot OrlixKernel/include OrlixHostAdapter"; \
	c_files="$$(rg --files $$lint_roots | rg '\.(c|cc|cpp|cxx)$$' || true)"; \
	header_files="$$(rg --files $$lint_roots | rg '\.h$$' || true)"; \
	objc_files="$$(rg --files $$lint_roots | rg '\.(m|mm)$$' || true)"; \
	if [ -n "$$c_files" ]; then \
		while IFS= read -r file; do \
			flags="$(ORLIX_LINT_C_FLAGS)"; \
			if [[ "$$file" == boot/* ]]; then \
				flags="$(ORLIX_LINT_HOST_C_FLAGS)"; \
			elif [[ "$$file" == OrlixHostAdapter/* || "$$file" == OrlixHostAdapterTests/* ]]; then \
				flags="$(ORLIX_LINT_HOST_C_FLAGS)"; \
				if [[ "$$file" == OrlixHostAdapter/fs/open_flags.c || "$$file" == OrlixHostAdapter/fs/backing_stat_translate.c ]]; then \
					flags="$$flags $(ORLIX_LINT_LINUX_HEADER_FLAGS)"; \
				fi; \
				if [[ "$$file" == OrlixHostAdapter/kernel/* ]]; then \
					flags="$(ORLIX_LINT_HOST_KERNEL_C_FLAGS)"; \
				fi; \
			elif [[ "$$file" == OrlixKernelTests/* ]]; then \
				flags="$(ORLIX_LINT_KERNEL_TEST_STRICT_C_FLAGS)"; \
				if [[ "$$file" == OrlixKernelTests/LinuxUAPICompileSmoke.c || "$$file" == OrlixKernelTests/LinuxUAPITestSupport.c ]]; then \
					flags="$(ORLIX_LINT_KERNEL_TEST_C_FLAGS)"; \
				elif [[ "$$file" == OrlixKernelTests/PTYSessionIoctlShim.c ]]; then \
					flags="$(ORLIX_LINT_HOST_C_FLAGS)"; \
				fi; \
			fi; \
			"$(CLANG_TIDY)" --load="$$plugin_path" --config-file=.clang-tidy "$$file" -- $$flags; \
		done <<< "$$c_files"; \
	fi; \
	if [ -n "$$header_files" ]; then \
		while IFS= read -r file; do \
			flags="$(ORLIX_LINT_C_FLAGS)"; \
			if [[ "$$file" == OrlixHostAdapter/* || "$$file" == OrlixHostAdapterTests/* ]]; then \
				flags="$(ORLIX_LINT_HOST_C_FLAGS)"; \
				if [[ "$$file" == OrlixHostAdapter/fs/open_flags.c || "$$file" == OrlixHostAdapter/fs/backing_stat_translate.c ]]; then \
					flags="$$flags $(ORLIX_LINT_LINUX_HEADER_FLAGS)"; \
				fi; \
				if [[ "$$file" == OrlixHostAdapter/kernel/* ]]; then \
					flags="$(ORLIX_LINT_HOST_KERNEL_C_FLAGS)"; \
				fi; \
			fi; \
			"$(CLANG_TIDY)" --load="$$plugin_path" --config-file=.clang-tidy "$$file" -- $$flags -x c-header; \
		done <<< "$$header_files"; \
	fi; \
	if [ -n "$$objc_files" ]; then \
		while IFS= read -r file; do \
			flags="$(ORLIX_LINT_OBJC_FLAGS)"; \
			if [[ "$$file" == OrlixHostAdapterTests/* ]]; then \
				flags="$$flags $(ORLIX_LINT_LINUX_HEADER_FLAGS)"; \
			fi; \
			"$(CLANG_TIDY)" --load="$$plugin_path" --config-file=.clang-tidy "$$file" -- $$flags; \
		done <<< "$$objc_files"; \
	fi

lint-linux-surface: lint
