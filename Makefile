SHELL := /bin/bash

LINUX_VERSION ?= 6.12
LINUX_ARCH ?= arm64
LINUX_TAG ?= v$(LINUX_VERSION)
LINUX_REMOTE ?= https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git
LINUX_UPSTREAM_DIR ?= Linux/upstream/linux-$(LINUX_VERSION)
LINUX_WORK_DIR ?= Build/linux-work
ORLIX_LINUX_OVERLAY ?= Linux/ports/orlix/overlay
ORLIX_LINUX_PATCH_DIR ?= Linux/ports/orlix/patches
ORLIX_XCFRAMEWORK_DIR ?= Build/OrlixKernel.xcframework
LINUX_KERNEL_SERIES ?=
LINUX_TARBALL_URL ?=
LINUX_KERNEL_VENDOR_ROOT ?= OrlixKernel/vendor/linux
KEEP_LINUX_TMP ?= 0
LINUX_MAKE ?=
LINUX_LLVM_BIN ?=
LINUX_SED ?=
LLVM_PREFIX ?= /opt/homebrew/opt/llvm
CLANG_TIDY ?= $(LLVM_PREFIX)/bin/clang-tidy
LLVM_CONFIG ?= $(LLVM_PREFIX)/bin/llvm-config
CLANG_TIDY_BUILD_DIR ?= .clang-tidy-build
CLANG_TIDY_PLUGIN_SO := $(CLANG_TIDY_BUILD_DIR)/OrlixTidyModule.so
CLANG_TIDY_PLUGIN_DYLIB := $(CLANG_TIDY_BUILD_DIR)/OrlixTidyModule.dylib
IPHONESIM_SDK := /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator26.4.sdk
IPHONESIM_FRAMEWORK_DIR := /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/Library/Frameworks
IPHONESIM_SDK_FRAMEWORK_DIR := /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator26.4.sdk/Developer/Library/Frameworks
LINUX_KERNEL_TUPLE_ROOT := $(CURDIR)/$(LINUX_KERNEL_VENDOR_ROOT)/$(LINUX_VERSION)/$(LINUX_ARCH)/kheaders
LINUX_KERNEL_KHEADERS_INCLUDE_ROOT := $(LINUX_KERNEL_TUPLE_ROOT)/include
LINUX_KERNEL_UAPI_INCLUDE_ROOT := $(LINUX_KERNEL_KHEADERS_INCLUDE_ROOT)/uapi
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
ORLIX_LINT_KERNEL_VENDOR_FLAGS := \
	-I$(LINUX_KERNEL_KHEADERS_INCLUDE_ROOT) \
	-I$(LINUX_KERNEL_UAPI_INCLUDE_ROOT)
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
	$(ORLIX_LINT_KERNEL_VENDOR_FLAGS) \
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
	$(ORLIX_LINT_KERNEL_VENDOR_FLAGS)
ORLIX_LINT_KERNEL_TEST_STRICT_C_FLAGS := \
	$(ORLIX_LINT_HOST_KERNEL_C_FLAGS) \
	-include $(LINUX_HOST_COMPAT_INCLUDE_ROOT)/macho_compat.h \
	$(ORLIX_LINT_KERNEL_VENDOR_FLAGS)
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

.PHONY: build-orlix-clang-tidy-module lint lint-linux-surface bootstrap-linux-upstream prepare-linux-worktree

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

lint: build-orlix-clang-tidy-module
	@set -euo pipefail; \
	plugin_path="$(CLANG_TIDY_PLUGIN_SO)"; \
	if [ ! -f "$$plugin_path" ]; then \
		plugin_path="$(CLANG_TIDY_PLUGIN_DYLIB)"; \
	fi; \
	if [ ! -f "$$plugin_path" ]; then \
		echo "clang-tidy plugin not found" >&2; \
		exit 1; \
	fi; \
	c_files="$$(rg --files OrlixKernel OrlixHostAdapter OrlixKernelTests OrlixHostAdapterTests | rg '\.(c|cc|cpp|cxx)$$' | rg -v '^OrlixKernel/vendor/' || true)"; \
	header_files="$$(rg --files OrlixKernel OrlixHostAdapter OrlixKernelTests OrlixHostAdapterTests | rg '\.h$$' | rg -v '^OrlixKernel/vendor/' || true)"; \
	objc_files="$$(rg --files OrlixKernel OrlixHostAdapter OrlixKernelTests OrlixHostAdapterTests | rg '\.(m|mm)$$' | rg -v '^OrlixKernel/vendor/' || true)"; \
	if [ -n "$$c_files" ]; then \
		while IFS= read -r file; do \
			flags="$(ORLIX_LINT_C_FLAGS)"; \
			if [[ "$$file" == OrlixHostAdapter/* || "$$file" == OrlixHostAdapterTests/* ]]; then \
				flags="$(ORLIX_LINT_HOST_C_FLAGS)"; \
				if [[ "$$file" == OrlixHostAdapter/fs/open_flags.c || "$$file" == OrlixHostAdapter/fs/backing_stat_translate.c ]]; then \
					flags="$$flags $(ORLIX_LINT_KERNEL_VENDOR_FLAGS)"; \
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
					flags="$$flags $(ORLIX_LINT_KERNEL_VENDOR_FLAGS)"; \
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
				flags="$$flags $(ORLIX_LINT_KERNEL_VENDOR_FLAGS)"; \
			fi; \
			"$(CLANG_TIDY)" --load="$$plugin_path" --config-file=.clang-tidy "$$file" -- $$flags; \
		done <<< "$$objc_files"; \
	fi

lint-linux-surface: lint

.PHONY: vendor-orlixkernel-linux-headers _vendor-linux-tree

vendor-orlixkernel-linux-headers:
	@$(MAKE) _vendor-linux-tree LINUX_VENDOR_MODE=kernel

_vendor-linux-tree:
	@set -euo pipefail; \
	repo_root="$$(pwd)"; \
	linux_version="$(LINUX_VERSION)"; \
	linux_arch="$(LINUX_ARCH)"; \
	kernel_series="$(LINUX_KERNEL_SERIES)"; \
	linux_vendor_mode="$(LINUX_VENDOR_MODE)"; \
	keep_tmp="$(KEEP_LINUX_TMP)"; \
	linux_make="$(LINUX_MAKE)"; \
	llvm_bin="$(LINUX_LLVM_BIN)"; \
	linux_sed="$(LINUX_SED)"; \
	case "$$linux_vendor_mode" in \
		kernel) vendor_root="$(LINUX_KERNEL_VENDOR_ROOT)" ;; \
		*) echo "unsupported Linux vendor mode: $$linux_vendor_mode" >&2; exit 1 ;; \
	esac; \
	case "$$linux_arch" in \
		arm64) ;; \
		*) echo "unsupported Linux arch: $$linux_arch" >&2; exit 1 ;; \
	esac; \
	if [ -z "$$kernel_series" ]; then \
		kernel_series="v$${linux_version%%.*}.x"; \
	fi; \
	if [ -z "$$llvm_bin" ] && [ -d /opt/homebrew/opt/llvm/bin ]; then \
		llvm_bin=/opt/homebrew/opt/llvm/bin; \
	fi; \
	if [ -n "$$llvm_bin" ]; then \
		export PATH="$$llvm_bin:$$PATH"; \
	fi; \
	if [ -z "$$linux_sed" ] && command -v gsed >/dev/null 2>&1; then \
		linux_sed="$$(command -v gsed)"; \
	fi; \
	if [ -z "$$linux_make" ]; then \
		if command -v gmake >/dev/null 2>&1; then \
			linux_make="$$(command -v gmake)"; \
		else \
			linux_make="$$(command -v make)"; \
		fi; \
	fi; \
	if ! "$$linux_make" --version 2>/dev/null | head -n1 | grep -Eq 'GNU Make ([4-9]|[1-9][0-9])'; then \
		echo "Linux vendoring requires GNU Make >= 4.0; found: $$("$$linux_make" --version | head -n1)" >&2; \
		echo "Set LINUX_MAKE=/path/to/gmake and rerun." >&2; \
		exit 1; \
	fi; \
	if ! command -v clang >/dev/null 2>&1 || ! command -v ld.lld >/dev/null 2>&1; then \
		echo "Linux vendoring requires clang and ld.lld in PATH." >&2; \
		echo "Set LINUX_LLVM_BIN=/path/to/llvm/bin and rerun." >&2; \
		exit 1; \
	fi; \
	if ! command -v rsync >/dev/null 2>&1; then \
		echo "Linux vendoring requires rsync in PATH." >&2; \
		exit 1; \
	fi; \
	if [ -n "$(LINUX_TARBALL_URL)" ]; then \
		tarball_url="$(LINUX_TARBALL_URL)"; \
	else \
		tarball_url="https://cdn.kernel.org/pub/linux/kernel/$$kernel_series/linux-$$linux_version.tar.xz"; \
	fi; \
	if [[ "$$vendor_root" = /* ]]; then \
		final_vendor_root="$$vendor_root"; \
	else \
		final_vendor_root="$$repo_root/$$vendor_root"; \
	fi; \
	final_tuple_root="$$final_vendor_root/$$linux_version/$$linux_arch"; \
	if [ "$$linux_vendor_mode" = "kernel" ]; then \
		final_tuple_root="$$final_tuple_root/kheaders"; \
	fi; \
	rm -rf "$$final_tuple_root"; \
	mkdir -p "$$(dirname "$$final_tuple_root")"; \
	tmp="$$(mktemp -d "$${TMPDIR:-/tmp}/vendor-linux-headers.XXXXXX")"; \
	cleanup() { \
		if [ "$$keep_tmp" = "1" ]; then \
			echo "kept temporary directory: $$tmp"; \
			return; \
		fi; \
		rm -rf "$$tmp"; \
	}; \
	trap cleanup EXIT; \
	require_file() { \
		local path="$$1"; \
		if [ ! -f "$$path" ]; then \
			echo "missing required file: $$path" >&2; \
			exit 1; \
		fi; \
	}; \
	require_dir() { \
		local path="$$1"; \
		if [ ! -d "$$path" ]; then \
			echo "missing required directory: $$path" >&2; \
			exit 1; \
		fi; \
	}; \
	copy_file() { \
		local src="$$1"; \
		local dest="$$2"; \
		require_file "$$src"; \
		mkdir -p "$$(dirname "$$dest")"; \
		cp "$$src" "$$dest"; \
	}; \
	rewrite_flat_kernel_generated_includes() { \
		local include_root="$$1"; \
		find "$$include_root" -type f \( -name '*.h' -o -name '*.lds' -o -name '*.cmd' \) -print0 \
			| xargs -0 perl -0pi -e 's{([<"])generated/([^">]+)([>"])}{$$1$$2$$3}g'; \
	}; \
		copy_tree() { \
			local src="$$1"; \
			local dest="$$2"; \
			require_dir "$$src"; \
			mkdir -p "$$dest"; \
			cp -R "$$src/." "$$dest"; \
		}; \
		write_source_json() { \
			local tuple_root="$$1"; \
			local tarball_sha="$$2"; \
			printf '%s\n' \
				"{" \
				"  \"linux_version\": \"$$linux_version\"," \
				"  \"linux_arch\": \"$$linux_arch\"," \
				"  \"kernel_series\": \"$$kernel_series\"," \
				"  \"tarball_url\": \"$$tarball_url\"," \
				"  \"tarball_sha256\": \"$$tarball_sha\"," \
				"  \"generated_by\": \"Makefile:vendor-linux-headers\"," \
				"  \"make_targets\": [" \
				"    \"defconfig\"," \
				"    \"olddefconfig\"," \
				"    \"prepare\"," \
				"    \"modules_prepare\"" \
				"  ]" \
				"}" \
				> "$$tuple_root/source.json"; \
		}; \
		write_readme() { \
			local out_file="$$1"; \
			local surface_label="$$2"; \
			local surface_desc="$$3"; \
			printf '%s\n' \
				"# Vendored Linux Headers" \
			"" \
			"This tuple was generated from upstream Linux sources." \
			"Do not edit vendored files manually." \
			"" \
			"Regenerate with:" \
			"" \
				"\`\`\`sh" \
				"make vendor-orlixkernel-linux-headers LINUX_VERSION=<version> LINUX_ARCH=<arch>" \
				"\`\`\`" \
				"" \
				"Surface: $$surface_label" \
				"" \
				"- $$surface_desc" \
				> "$$out_file"; \
		}; \
		write_manifest() { \
			local tuple_root="$$1"; \
			local manifest_file="$$2"; \
			local rel; \
			( \
				cd "$$tuple_root"; \
				find . -type f ! -name manifest.sha256 -print \
					| LC_ALL=C sort \
					| while IFS= read -r rel; do \
						rel="$${rel#./}"; \
						printf '%s  %s\n' "$$(shasum -a 256 "$$tuple_root/$$rel" | awk '{print $$1}')" "$$rel"; \
					done \
			) > "$$manifest_file"; \
		}; \
		validate_kernel_vendor_tree() { \
			local tuple_root="$$1"; \
			local include_root="$$tuple_root/include"; \
			if [ -z "$$(find "$$tuple_root" -type f -print -quit)" ]; then \
				echo "empty tuple root: $$tuple_root" >&2; \
				exit 1; \
			fi; \
			require_file "$$include_root/linux/fs.h"; \
			require_file "$$include_root/linux/sched.h"; \
			require_dir "$$include_root/asm"; \
			require_dir "$$include_root/uapi/asm"; \
			require_dir "$$include_root/uapi/asm-generic"; \
			require_file "$$include_root/uapi/linux/types.h"; \
			require_file "$$include_root/uapi/asm/signal.h"; \
			require_file "$$include_root/uapi/asm/ucontext.h"; \
			require_file "$$include_root/uapi/linux/time_types.h"; \
			if grep -q 'WITH Linux-syscall-note' "$$include_root/linux/types.h"; then \
				echo "unexpected UAPI linux/types.h in OrlixKernel vendor root" >&2; \
				exit 1; \
			fi; \
			require_file "$$include_root/autoconf.h"; \
			require_file "$$include_root/utsrelease.h"; \
			require_dir "$$include_root/config"; \
			if [ -e "$$include_root/generated" ]; then \
				echo "unexpected generated surface in OrlixKernel vendor root: $$include_root/generated" >&2; \
				exit 1; \
			fi; \
		}; \
	tarball="$$tmp/linux-$$linux_version.tar.xz"; \
	src="$$tmp/linux-$$linux_version"; \
	obj="$$tmp/obj-$$linux_version-$$linux_arch"; \
	stage_vendor_root="$$tmp/vendor-root"; \
	host_compat_include="$$tmp/host-compat/include"; \
	host_tool_bin="$$tmp/host-tools/bin"; \
	linux_make_env=(LLVM=1 LLVM_IAS=1 HOSTCC=clang CC=clang LD=ld.lld); \
	mkdir -p "$$host_compat_include" "$$host_tool_bin" "$$stage_vendor_root"; \
	curl -fL --retry 5 --retry-delay 1 --retry-all-errors "$$tarball_url" -o "$$tarball"; \
	tarball_sha="$$(shasum -a 256 "$$tarball" | awk '{print $$1}')"; \
		tar -xf "$$tarball" -C "$$tmp"; \
		require_dir "$$src"; \
		cp "$$repo_root/tools/linux_host_compat/include/elf.h" "$$host_compat_include/elf.h"; \
		cp "$$repo_root/tools/linux_host_compat/include/endian.h" "$$host_compat_include/endian.h"; \
		cp "$$repo_root/tools/linux_host_compat/include/byteswap.h" "$$host_compat_include/byteswap.h"; \
		cp "$$repo_root/tools/linux_host_compat/include/linux_arm_elf_compat.h" "$$host_compat_include/linux_arm_elf_compat.h"; \
	perl -0pi -e 's/#include "modpost.h"/#define _UUID_T\n#define uuid_t int\n#include "modpost.h"\n#undef uuid_t/' "$$src/scripts/mod/file2alias.c"; \
	linux_make_env+=("HOSTCFLAGS=-I$$host_compat_include -include $$host_compat_include/linux_arm_elf_compat.h"); \
	if [ -n "$$linux_sed" ]; then \
		ln -sf "$$linux_sed" "$$host_tool_bin/sed"; \
		export PATH="$$host_tool_bin:$$PATH"; \
	fi; \
	"$$linux_make" -C "$$src" O="$$obj" ARCH="$$linux_arch" "$${linux_make_env[@]}" defconfig; \
	if [ -x "$$src/scripts/config" ]; then \
		"$$src/scripts/config" --file "$$obj/.config" -d BUILDTIME_TABLE_SORT || true; \
	fi; \
	"$$linux_make" -C "$$src" O="$$obj" ARCH="$$linux_arch" "$${linux_make_env[@]}" olddefconfig; \
	"$$linux_make" -C "$$src" O="$$obj" ARCH="$$linux_arch" "$${linux_make_env[@]}" prepare; \
	"$$linux_make" -C "$$src" O="$$obj" ARCH="$$linux_arch" "$${linux_make_env[@]}" modules_prepare; \
	require_dir "$$src/include"; \
	require_dir "$$src/arch/$$linux_arch/include"; \
		require_dir "$$obj/include/generated"; \
		require_dir "$$obj/include/config"; \
		require_dir "$$obj/arch/$$linux_arch/include/generated"; \
		tuple_stage="$$stage_vendor_root/$$linux_version/$$linux_arch"; \
		if [ "$$linux_vendor_mode" = "kernel" ]; then \
			tuple_stage="$$tuple_stage/kheaders"; \
		fi; \
		include_root="$$tuple_stage/include"; \
		if [ "$$linux_vendor_mode" = "kernel" ]; then \
			copy_tree "$$src/include" "$$include_root"; \
			copy_tree "$$src/arch/$$linux_arch/include" "$$include_root"; \
			copy_tree "$$obj/include/generated" "$$include_root"; \
			copy_tree "$$obj/include/config" "$$include_root/config"; \
			copy_tree "$$obj/arch/$$linux_arch/include/generated" "$$include_root"; \
			copy_tree "$$src/include/uapi" "$$include_root/uapi"; \
			copy_tree "$$src/arch/$$linux_arch/include/uapi" "$$include_root/uapi"; \
			copy_tree "$$obj/include/generated/uapi" "$$include_root/uapi"; \
			copy_tree "$$obj/arch/$$linux_arch/include/generated/uapi" "$$include_root/uapi"; \
			rm -rf "$$include_root/generated"; \
			rewrite_flat_kernel_generated_includes "$$include_root"; \
			write_readme "$$tuple_stage/README.md" "include" "\`include\`: flattened Linux kernel header root for OrlixKernel, including the upstream \`uapi/\` subtree required by the full Linux header graph; generated staging namespaces are excluded."; \
			validate_kernel_vendor_tree "$$tuple_stage"; \
		fi; \
		write_source_json "$$tuple_stage" "$$tarball_sha"; \
		write_manifest "$$tuple_stage" "$$tuple_stage/manifest.sha256"; \
		rsync -a --delete "$$tuple_stage/" "$$final_tuple_root/"; \
		echo "vendored Linux tuple:"; \
		echo "  $$vendor_root/$$linux_version/$$linux_arch$$( [ "$$linux_vendor_mode" = "kernel" ] && printf '/kheaders' )"; \
		echo "surface:"; \
		echo "  include"
