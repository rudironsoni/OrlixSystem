# Kbuild vmlinux Proof Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the skeleton build/package flow with an honest Milestone 1 proof path that prepares the Orlix kernel port tree and invokes upstream Linux Kbuild `vmlinux` for `ARCH=orlix`.

**Architecture:** This plan keeps Milestone 1 focused on source-tree generation, profile wiring, Kbuild proof, and removal of misleading boot-stub product proof. It also rewrites the narrow bootloader contract test away from raw public boot params so the old product API does not remain locked in while full profile-DTS boot generation stays in Milestone 2.

**Tech Stack:** GNU Make/Kbuild, upstream Linux 6.12, Clang/LLVM, shell contract tests, C bootloader contract test.

---

## File Structure

- Modify: `Makefile` owns Milestone 1 proof targets, profile validation, port-tree generation, and Kbuild `vmlinux` invocation.
- Modify: `OrlixKernel/include/OrlixKernel.h` owns the public bootloader-shaped product API.
- Modify: `boot/loader.c` owns the minimal public `OrlixBoot` entrypoint behavior for the contract test.
- Modify: `boot/params.c` owns validation for the minimal boot config until Milestone 2 replaces validation-only boot preparation with profile-DTS boot input generation.
- Modify: `tests/bootloader_contract.c` owns the C-level bootloader API contract test.
- Create: `tests/milestone1_makefile_contract.sh` owns Makefile target-name and dry-run contract checks.
- Delete: `Linux/ports/orlix/overlay/arch/orlix/configs/defconfig` removes the committed one-line defconfig from the overlay; selected profile defconfigs are materialized during port preparation.
- Delete: `project.yml` removes the stale boot-stub Xcode product target until packaging depends on a real Linux artifact.

## Task 1: Add Milestone 1 Makefile Contract Test

**Files:**
- Create: `tests/milestone1_makefile_contract.sh`

- [ ] **Step 1: Create the failing shell contract test**

```bash
#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

fail() {
    printf 'FAIL: %s\n' "$1" >&2
    exit 1
}

expect_fail_contains() {
    local expected="$1"
    shift
    local output
    local status

    set +e
    output="$({ "$@"; } 2>&1)"
    status=$?
    set -e

    if [ "$status" -eq 0 ]; then
        printf '%s\n' "$output" >&2
        fail "expected command to fail: $*"
    fi

    case "$output" in
        *"$expected"*) ;;
        *)
            printf '%s\n' "$output" >&2
            fail "expected output containing: $expected"
            ;;
    esac
}

expect_success_contains() {
    local expected="$1"
    shift
    local output

    output="$({ "$@"; } 2>&1)"
    case "$output" in
        *"$expected"*) ;;
        *)
            printf '%s\n' "$output" >&2
            fail "expected output containing: $expected"
            ;;
    esac
}

expect_fail_contains "unsupported PROFILE=bogus" make validate-orlix-profile PROFILE=bogus
expect_success_contains "Build/OrlixKernel/linux-6.12-port" make -n prepare-orlixkernel-port PROFILE=appstore
expect_success_contains "Linux/ports/orlix/configs/appstore_defconfig" make -n prepare-orlixkernel-port PROFILE=appstore
expect_success_contains "vmlinux" make -n build-linux-kernel PROFILE=appstore
expect_success_contains "Build/OrlixKernel/build/appstore" make -n build-linux-kernel PROFILE=appstore
expect_success_contains "Build/OrlixKernel/build/development" make -n build-linux-kernel PROFILE=development

expect_fail_contains "No rule to make target" make -n prepare-linux-worktree
expect_fail_contains "No rule to make target" make -n build-linux-orlix-kernel-simulator
expect_fail_contains "No rule to make target" make -n build-linux-simulator
expect_fail_contains "No rule to make target" make -n build-linux-iphoneos
expect_fail_contains "No rule to make target" make -n package-orlixkernel-xcframework

if [ -e Linux/ports/orlix/overlay/arch/orlix/configs/defconfig ]; then
    fail "profile defconfig must not be committed under the arch overlay"
fi
```

- [ ] **Step 2: Make the shell test executable**

Run:

```bash
chmod +x tests/milestone1_makefile_contract.sh
```

Expected: no output and exit 0.

- [ ] **Step 3: Run the test to verify it fails before implementation**

Run:

```bash
tests/milestone1_makefile_contract.sh
```

Expected: FAIL because `validate-orlix-profile`, `prepare-orlixkernel-port`, and `build-linux-kernel` do not exist yet, and old targets still exist.

- [ ] **Step 4: Commit the failing test**

```bash
rtk git add tests/milestone1_makefile_contract.sh
rtk git commit -m "test: add milestone one build contract"
```

## Task 2: Replace Makefile Skeleton Targets With Milestone 1 Proof Targets

**Files:**
- Modify: `Makefile`

- [ ] **Step 1: Replace the Makefile variable block and phony target list**

Replace lines 3-28 of `Makefile` with:

```make
LINUX_VERSION ?= 6.12
LINUX_ARCH ?= orlix
LINUX_TAG ?= v$(LINUX_VERSION)
LINUX_REMOTE ?= https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git

PROFILE ?= appstore
ORLIX_PROFILES := appstore development enterprise

LINUX_UPSTREAM_DIR ?= Linux/upstream/linux-$(LINUX_VERSION)
LINUX_CLONE_CACHE_DIR ?= .cache/linux-clone
LINUX_CLONE_CACHE_REPO := $(CURDIR)/$(LINUX_CLONE_CACHE_DIR)/linux-$(LINUX_VERSION).git

ORLIX_LINUX_OVERLAY ?= Linux/ports/orlix/overlay
ORLIX_LINUX_PATCH_DIR ?= Linux/ports/orlix/patches
ORLIX_PROFILE_CONFIG := Linux/ports/orlix/configs/$(PROFILE)_defconfig
ORLIX_KERNEL_HEADER ?= OrlixKernel/include/OrlixKernel.h

ORLIX_KERNEL_PORT_DIR ?= Build/OrlixKernel/linux-$(LINUX_VERSION)-port
ORLIX_KERNEL_BUILD_ROOT := $(CURDIR)/Build/OrlixKernel/build
ORLIX_KERNEL_BUILD_DIR := $(ORLIX_KERNEL_BUILD_ROOT)/$(PROFILE)
ORLIX_KERNEL_VMLINUX := $(ORLIX_KERNEL_BUILD_DIR)/vmlinux
ORLIX_BOOT_CONTRACT_DIR := $(CURDIR)/Build/OrlixKernel/bootloader-contract

LINUX_MAKE ?=
LINUX_SED ?=
LINUX_LLVM_BIN ?= $(shell if command -v llvm-ar >/dev/null 2>&1; then dirname "$$(command -v llvm-ar)"; elif [ -x /opt/homebrew/opt/llvm/bin/llvm-ar ]; then printf '%s\n' /opt/homebrew/opt/llvm/bin; fi)
LINUX_HOST_COMPAT_INCLUDE_ROOT := $(CURDIR)/tools/linux_host_compat/include

.PHONY: bootstrap-linux-upstream validate-orlix-profile prepare-orlixkernel-port build-linux-kernel test-bootloader-contract test-milestone1-contract
```

- [ ] **Step 2: Add profile validation after `bootstrap-linux-upstream`**

Insert this target immediately after the `bootstrap-linux-upstream` target:

```make
validate-orlix-profile:
	@set -euo pipefail; \
	profile="$(PROFILE)"; \
	case " $(ORLIX_PROFILES) " in \
		*" $$profile "*) ;; \
		*) echo "unsupported PROFILE=$$profile (expected one of: $(ORLIX_PROFILES))" >&2; exit 1 ;; \
	esac; \
	config="$(ORLIX_PROFILE_CONFIG)"; \
	[ -f "$$config" ] || { echo "missing profile defconfig: $$config" >&2; exit 1; }
```

- [ ] **Step 3: Replace `prepare-linux-worktree` with `prepare-orlixkernel-port`**

Replace the entire `prepare-linux-worktree` target with:

```make
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
```

- [ ] **Step 4: Replace all old build/package targets with `build-linux-kernel` and the test target**

Delete the targets named `build-linux-orlix-kernel-simulator`, `build-linux-simulator`, `build-linux-iphoneos`, `build-static-library`, and `package-orlixkernel-xcframework`.

Add this target after `prepare-orlixkernel-port`:

```make
build-linux-kernel: prepare-orlixkernel-port
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
	build_dir="$(ORLIX_KERNEL_BUILD_DIR)"; \
	rm -rf "$$build_dir"; \
	mkdir -p "$$build_dir"; \
	"$$linux_make" -C "$(ORLIX_KERNEL_PORT_DIR)" O="$$build_dir" ARCH="$(LINUX_ARCH)" LLVM=1 CLANG_TARGET_FLAGS=aarch64-linux-gnu HOSTCFLAGS="-I$(LINUX_HOST_COMPAT_INCLUDE_ROOT) -include linux_arm_elf_compat.h -D_UUID_T" mrproper defconfig vmlinux; \
	[ -f "$(ORLIX_KERNEL_VMLINUX)" ] || { echo "missing vmlinux artifact: $(ORLIX_KERNEL_VMLINUX)" >&2; exit 1; }; \
	echo "Linux vmlinux ready: $(ORLIX_KERNEL_VMLINUX) (profile $(PROFILE))"
```

Replace the `test-bootloader-contract` target with this narrower compile target:

```make
test-bootloader-contract:
	@set -euo pipefail; \
	build_dir="$(ORLIX_BOOT_CONTRACT_DIR)"; \
	mkdir -p "$$build_dir"; \
	$(CC) -std=c11 -Wall -Wextra -Werror \
		-IOrlixKernel/include \
		boot/loader.c \
		boot/params.c \
		tests/bootloader_contract.c \
		-o "$$build_dir/bootloader_contract"; \
	"$$build_dir/bootloader_contract"

test-milestone1-contract:
	@tests/milestone1_makefile_contract.sh
```

- [ ] **Step 5: Run the Makefile contract test**

Run:

```bash
tests/milestone1_makefile_contract.sh
```

Expected: FAIL only because `Linux/ports/orlix/overlay/arch/orlix/configs/defconfig` still exists.

## Task 3: Remove Committed Overlay Default Defconfig

**Files:**
- Delete: `Linux/ports/orlix/overlay/arch/orlix/configs/defconfig`

- [ ] **Step 1: Delete the overlay default defconfig**

Remove:

```text
Linux/ports/orlix/overlay/arch/orlix/configs/defconfig
```

- [ ] **Step 2: Run the Makefile contract test**

Run:

```bash
tests/milestone1_makefile_contract.sh
```

Expected: PASS.

- [ ] **Step 3: Commit Makefile target replacement**

```bash
rtk git add Makefile Linux/ports/orlix/overlay/arch/orlix/configs/defconfig tests/milestone1_makefile_contract.sh
rtk git commit -m "build: add Orlix kernel vmlinux proof target"
```

## Task 4: Rewrite Bootloader Contract Away From Raw Public Boot Params

**Files:**
- Modify: `OrlixKernel/include/OrlixKernel.h`
- Modify: `boot/loader.c`
- Modify: `boot/params.c`
- Modify: `tests/bootloader_contract.c`

- [ ] **Step 1: Replace the public header**

Replace `OrlixKernel/include/OrlixKernel.h` with:

```c
#ifndef ORLIX_KERNEL_H
#define ORLIX_KERNEL_H

#ifdef __cplusplus
extern "C" {
#endif

enum OrlixBootProfile {
    ORLIX_BOOT_PROFILE_APPSTORE = 0,
    ORLIX_BOOT_PROFILE_DEVELOPMENT = 1,
    ORLIX_BOOT_PROFILE_ENTERPRISE = 2,
};

struct OrlixBootConfig {
    enum OrlixBootProfile profile;
    const char *root_image_identifier;
    const char *terminal_identifier;
};

__attribute__((visibility("default"))) int OrlixBoot(const struct OrlixBootConfig *config);

#ifdef __cplusplus
}
#endif

#endif
```

- [ ] **Step 2: Replace `boot/params.c` with boot-config validation**

Replace `boot/params.c` with:

```c
#include "OrlixKernel.h"

static int OrlixBootProfileIsValid(enum OrlixBootProfile profile) {
    switch (profile) {
    case ORLIX_BOOT_PROFILE_APPSTORE:
    case ORLIX_BOOT_PROFILE_DEVELOPMENT:
    case ORLIX_BOOT_PROFILE_ENTERPRISE:
        return 1;
    }
    return 0;
}

static int OrlixBootStringIsPresent(const char *value) {
    return value && value[0] != '\0';
}

__attribute__((visibility("hidden"))) int OrlixPrepareBootConfig(const struct OrlixBootConfig *config) {
    if (!config) {
        return -1;
    }
    if (!OrlixBootProfileIsValid(config->profile)) {
        return -1;
    }
    if (!OrlixBootStringIsPresent(config->root_image_identifier)) {
        return -1;
    }
    if (!OrlixBootStringIsPresent(config->terminal_identifier)) {
        return -1;
    }
    return 0;
}
```

- [ ] **Step 3: Replace `boot/loader.c` with a non-fake boot entrypoint**

Replace `boot/loader.c` with:

```c
#include "OrlixKernel.h"

__attribute__((visibility("hidden"))) int OrlixPrepareBootConfig(const struct OrlixBootConfig *config);

int OrlixBoot(const struct OrlixBootConfig *config) {
    if (OrlixPrepareBootConfig(config) != 0) {
        return -1;
    }

    return -1;
}
```

The valid-config path still returns failure because profile device tree generation and Linux handoff are Milestone 2 work. This avoids returning success from a boot entrypoint that did not boot Linux.

- [ ] **Step 4: Replace `tests/bootloader_contract.c`**

Replace `tests/bootloader_contract.c` with:

```c
#include "OrlixKernel.h"

int OrlixPrepareBootConfig(const struct OrlixBootConfig *config);

static int expect_invalid_config(const struct OrlixBootConfig *config)
{
    return OrlixPrepareBootConfig(config) == -1 ? 0 : -1;
}

int main(void)
{
    struct OrlixBootConfig config = {
        .profile = ORLIX_BOOT_PROFILE_APPSTORE,
        .root_image_identifier = "default-root",
        .terminal_identifier = "default-terminal",
    };

    if (expect_invalid_config(0) != 0) {
        return 1;
    }

    config.profile = (enum OrlixBootProfile)99;
    if (expect_invalid_config(&config) != 0) {
        return 2;
    }
    config.profile = ORLIX_BOOT_PROFILE_APPSTORE;

    config.root_image_identifier = 0;
    if (expect_invalid_config(&config) != 0) {
        return 3;
    }
    config.root_image_identifier = "default-root";

    config.root_image_identifier = "";
    if (expect_invalid_config(&config) != 0) {
        return 4;
    }
    config.root_image_identifier = "default-root";

    config.terminal_identifier = 0;
    if (expect_invalid_config(&config) != 0) {
        return 5;
    }
    config.terminal_identifier = "default-terminal";

    config.terminal_identifier = "";
    if (expect_invalid_config(&config) != 0) {
        return 6;
    }
    config.terminal_identifier = "default-terminal";

    if (OrlixPrepareBootConfig(&config) != 0) {
        return 7;
    }

    if (OrlixBoot(&config) != -1) {
        return 8;
    }

    return 0;
}
```

- [ ] **Step 5: Run the bootloader contract test**

Run:

```bash
make test-bootloader-contract
```

Expected: PASS.

- [ ] **Step 6: Verify the public header no longer exposes raw boot params**

Run:

```bash
! grep -R "struct boot_params\|OrlixPrepareBootParams" OrlixKernel/include boot tests/bootloader_contract.c
```

Expected: no output and exit 0.

- [ ] **Step 7: Commit bootloader contract rewrite**

```bash
rtk git add OrlixKernel/include/OrlixKernel.h boot/loader.c boot/params.c tests/bootloader_contract.c
rtk git commit -m "boot: define minimal Orlix boot config contract"
```

## Task 5: Remove Stale Boot-Stub Xcode Product Target

**Files:**
- Delete: `project.yml`

- [ ] **Step 1: Delete the stale XcodeGen product file**

Remove:

```text
project.yml
```

This removes the boot-stub static library target. A new Xcode packaging definition belongs in the later XCFramework packaging milestone after a real Linux artifact exists.

- [ ] **Step 2: Verify no docs point at the deleted project as current proof**

Run:

```bash
! grep -R "OrlixKernel.xcodeproj\|project.yml.*proof\|build-linux-simulator\|build-linux-iphoneos" README.md AGENTS.md docs
```

Expected: no output and exit 0.

- [ ] **Step 3: Commit project target removal**

```bash
rtk git add project.yml
rtk git commit -m "build: remove boot stub Xcode product target"
```

## Task 6: Run Milestone 1 Proof Commands

**Files:**
- Verify: `Makefile`
- Verify: generated `Build/OrlixKernel/linux-6.12-port`
- Verify: generated `Build/OrlixKernel/build/appstore/vmlinux`
- Verify: generated `Build/OrlixKernel/build/development/vmlinux`

- [ ] **Step 1: Verify the local contract tests**

Run:

```bash
make test-milestone1-contract
make test-bootloader-contract
```

Expected: both commands PASS.

- [ ] **Step 2: Prepare the App Store port tree**

Run:

```bash
make prepare-orlixkernel-port PROFILE=appstore
```

Expected: PASS with output containing:

```text
prepared Orlix kernel port tree: Build/OrlixKernel/linux-6.12-port (profile appstore)
```

- [ ] **Step 3: Verify the selected defconfig was materialized**

Run:

```bash
cmp -s Linux/ports/orlix/configs/appstore_defconfig Build/OrlixKernel/linux-6.12-port/arch/orlix/configs/defconfig
```

Expected: no output and exit 0.

- [ ] **Step 4: Build the App Store Linux kernel proof target**

Run:

```bash
make build-linux-kernel PROFILE=appstore
```

Expected: PASS with output containing:

```text
Linux vmlinux ready:
```

and this file exists:

```text
Build/OrlixKernel/build/appstore/vmlinux
```

- [ ] **Step 5: Build the development Linux kernel proof target**

Run:

```bash
make build-linux-kernel PROFILE=development
```

Expected: PASS with output containing:

```text
Linux vmlinux ready:
```

and this file exists:

```text
Build/OrlixKernel/build/development/vmlinux
```

- [ ] **Step 6: If Kbuild exposes missing `arch/orlix` contracts, stop and capture the first concrete failure**

Run this only if Step 4 or Step 5 fails:

```bash
make build-linux-kernel PROFILE=appstore
```

Expected: a concrete compiler, linker, Kconfig, or missing-file error from Kbuild. Do not patch around multiple guessed failures. Use the first concrete failure to write a focused follow-up plan or invoke systematic debugging before editing `arch/orlix`.

- [ ] **Step 7: Commit final proof changes after both profile builds pass**

```bash
rtk git status --short
rtk git add Makefile OrlixKernel/include/OrlixKernel.h boot/loader.c boot/params.c tests/bootloader_contract.c tests/milestone1_makefile_contract.sh Linux/ports/orlix/overlay/arch/orlix/configs/defconfig project.yml
rtk git commit -m "build: prove Orlix Linux vmlinux target"
```

Expected: commit succeeds only after `appstore` and `development` `vmlinux` builds pass.

## Self-Review Checklist

- Spec coverage: This plan implements Milestone 1 proof target names, profile default behavior, port-tree rename, profile defconfig materialization, boot-stub packaging removal, and bootloader contract cleanup away from raw public boot params.
- Deferred scope: Full profile DTS generation, virtio transport, root disks, root assembly, console, networking, entropy, external mounts, hibernation, and prototype directory deletion remain in later milestone plans.
- Placeholder scan: The plan contains no deferred code blocks. The only conditional step is the explicit Kbuild failure capture gate, because missing architecture contracts must be driven by actual Kbuild output.
- Type consistency: Public API names are consistently `enum OrlixBootProfile`, `struct OrlixBootConfig`, and `OrlixBoot`.
