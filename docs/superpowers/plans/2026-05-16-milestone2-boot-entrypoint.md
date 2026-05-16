# Milestone 2 Boot Entrypoint Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Milestone 2 boot-entrypoint proof: the public bootloader API accepts only closed Orlix profiles and app-level resource identifiers, then prepares Linux-shaped private boot inputs and durable profile device-tree sources for `ARCH=orlix`.

**Architecture:** Keep the public product surface bootloader-shaped. Add a private boot-input seam under `boot/` that maps `OrlixBootConfig` to profile defaults, Linux command-line defaults, and profile DTS paths without exposing raw `struct boot_params` publicly. Add profile DTS sources under the durable `arch/orlix` overlay; do not claim boot, device, MMU, or userspace runtime proof.

**Tech Stack:** C11 bootloader contract tests, GNU Make contract targets, upstream Linux 6.12 Kbuild overlay inputs, profile DTS source files.

---

## File Structure

- Create: `boot/input.h` owns the private bootloader-to-Linux boot input structure and hidden preparation API.
- Modify: `boot/params.c` owns validation plus profile-to-Linux boot-input mapping.
- Modify: `boot/loader.c` owns the public `OrlixBoot` entrypoint and keeps valid configs returning `ORLIX_BOOT_STATUS_UNAVAILABLE` until a real execution substrate exists.
- Modify: `tests/bootloader_contract.c` owns the C contract for public validation and private boot-input preparation.
- Create: `tests/milestone2_boot_contract.sh` owns shell checks for profile DTS materialization and public API boundary enforcement.
- Modify: `Makefile` adds `test-milestone2-boot-contract`, compiles the bootloader contract with private boot headers, and keeps Milestone 1 proof targets unchanged.
- Create: `Linux/ports/orlix/overlay/arch/orlix/boot/dts/Makefile` registers profile DTS names inside the durable overlay.
- Create: `Linux/ports/orlix/overlay/arch/orlix/boot/dts/appstore.dts` owns App Store boot defaults.
- Create: `Linux/ports/orlix/overlay/arch/orlix/boot/dts/development.dts` owns Development boot defaults.
- Create: `Linux/ports/orlix/overlay/arch/orlix/boot/dts/enterprise.dts` owns Enterprise boot defaults.
- Modify: `README.md` documents Milestone 2 proof commands and keeps proof boundaries explicit.

## Task 1: Add A Failing Boot Input Contract Test

**Files:**
- Modify: `tests/bootloader_contract.c`
- Modify: `Makefile`

- [ ] **Step 1: Replace the bootloader contract test with the Milestone 2 contract**

Replace `tests/bootloader_contract.c` with:

```c
#include <string.h>

#include "OrlixKernel.h"
#include "boot/input.h"

int OrlixPrepareBootConfig(const struct OrlixBootConfig *config);

static int expect_invalid_config(const struct OrlixBootConfig *config)
{
    return OrlixBoot(config) == ORLIX_BOOT_STATUS_INVALID_CONFIG ? 0 : -1;
}

static int expect_string(const char *actual, const char *expected)
{
    if (!actual || !expected) {
        return -1;
    }
    return strcmp(actual, expected) == 0 ? 0 : -1;
}

static int expect_cmdline_contains(const char *actual, const char *fragment)
{
    if (!actual || !fragment) {
        return -1;
    }
    return strstr(actual, fragment) ? 0 : -1;
}

static int expect_profile_input(enum OrlixBootProfile profile,
                                const char *expected_dtb,
                                const char *expected_console,
                                const char *expected_root,
                                const char *expected_cmdline_fragment)
{
    struct OrlixBootConfig config = {
        .profile = profile,
        .root_image_identifier = "default-root",
        .terminal_identifier = "default-terminal",
    };
    struct OrlixBootInput input;

    if (OrlixPrepareBootInput(&config, &input) != ORLIX_BOOT_STATUS_OK) {
        return 1;
    }
    if (input.profile != profile) {
        return 2;
    }
    if (expect_string(input.profile_dtb_path, expected_dtb) != 0) {
        return 3;
    }
    if (expect_string(input.console_device, expected_console) != 0) {
        return 4;
    }
    if (expect_string(input.root_device, expected_root) != 0) {
        return 5;
    }
    if (expect_cmdline_contains(input.kernel_cmdline, expected_cmdline_fragment) != 0) {
        return 6;
    }
    if (input.root_image_identifier != config.root_image_identifier) {
        return 7;
    }
    if (input.terminal_identifier != config.terminal_identifier) {
        return 8;
    }

    return 0;
}

int main(void)
{
    struct OrlixBootConfig config = {
        .profile = ORLIX_BOOT_PROFILE_APPSTORE,
        .root_image_identifier = "default-root",
        .terminal_identifier = "default-terminal",
    };
    struct OrlixBootInput input;

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

    if (OrlixPrepareBootConfig(&config) != ORLIX_BOOT_STATUS_OK) {
        return 7;
    }

    if (OrlixPrepareBootInput(0, &input) != ORLIX_BOOT_STATUS_INVALID_CONFIG) {
        return 8;
    }

    if (OrlixPrepareBootInput(&config, 0) != ORLIX_BOOT_STATUS_INVALID_CONFIG) {
        return 9;
    }

    if (expect_profile_input(ORLIX_BOOT_PROFILE_APPSTORE,
                             "arch/orlix/boot/dts/appstore.dtb",
                             "ttyS0",
                             "/dev/ram0",
                             "orlix.profile=appstore") != 0) {
        return 10;
    }

    if (expect_profile_input(ORLIX_BOOT_PROFILE_DEVELOPMENT,
                             "arch/orlix/boot/dts/development.dtb",
                             "ttyS0",
                             "/dev/ram0",
                             "orlix.profile=development") != 0) {
        return 11;
    }

    if (expect_profile_input(ORLIX_BOOT_PROFILE_ENTERPRISE,
                             "arch/orlix/boot/dts/enterprise.dtb",
                             "ttyS0",
                             "/dev/ram0",
                             "orlix.profile=enterprise") != 0) {
        return 12;
    }

    if (OrlixBoot(&config) != ORLIX_BOOT_STATUS_UNAVAILABLE) {
        return 13;
    }

    return 0;
}
```

- [ ] **Step 2: Add the private include path to the bootloader contract build**

In `Makefile`, update the `test-bootloader-contract` compile command so the include block becomes:

```make
	$(CC) -std=c11 -Wall -Wextra -Werror \
		-I. \
		-IOrlixKernel/include \
		boot/loader.c \
		boot/params.c \
		tests/bootloader_contract.c \
		-o "$$build_dir/bootloader_contract"; \
```

- [ ] **Step 3: Run the bootloader contract to verify it fails for the expected reason**

Run:

```bash
rtk make test-bootloader-contract
```

Expected: FAIL with a compiler error containing `boot/input.h` because the private boot input header does not exist yet.

- [ ] **Step 4: Commit the failing contract**

Run:

```bash
rtk git add Makefile tests/bootloader_contract.c
rtk git commit -m "test: define milestone two boot input contract"
```

## Task 2: Implement Private Boot Input Preparation

**Files:**
- Create: `boot/input.h`
- Modify: `boot/params.c`
- Modify: `boot/loader.c`

- [ ] **Step 1: Create the private boot input header**

Create `boot/input.h` with:

```c
#ifndef ORLIX_BOOT_INPUT_H
#define ORLIX_BOOT_INPUT_H

#include "OrlixKernel.h"

struct OrlixBootInput {
    enum OrlixBootProfile profile;
    const char *profile_dtb_path;
    const char *kernel_cmdline;
    const char *root_device;
    const char *console_device;
    const char *root_image_identifier;
    const char *terminal_identifier;
};

__attribute__((visibility("hidden"))) int OrlixPrepareBootInput(
    const struct OrlixBootConfig *config,
    struct OrlixBootInput *input);

#endif
```

- [ ] **Step 2: Replace validation-only config preparation with boot input mapping**

Replace `boot/params.c` with:

```c
#include "boot/input.h"

struct OrlixProfileBootDefaults {
    enum OrlixBootProfile profile;
    const char *profile_dtb_path;
    const char *kernel_cmdline;
    const char *root_device;
    const char *console_device;
};

static const struct OrlixProfileBootDefaults OrlixProfileBootDefaultsTable[] = {
    {
        .profile = ORLIX_BOOT_PROFILE_APPSTORE,
        .profile_dtb_path = "arch/orlix/boot/dts/appstore.dtb",
        .kernel_cmdline = "console=ttyS0 root=/dev/ram0 rw orlix.profile=appstore",
        .root_device = "/dev/ram0",
        .console_device = "ttyS0",
    },
    {
        .profile = ORLIX_BOOT_PROFILE_DEVELOPMENT,
        .profile_dtb_path = "arch/orlix/boot/dts/development.dtb",
        .kernel_cmdline = "console=ttyS0 root=/dev/ram0 rw debug ignore_loglevel orlix.profile=development",
        .root_device = "/dev/ram0",
        .console_device = "ttyS0",
    },
    {
        .profile = ORLIX_BOOT_PROFILE_ENTERPRISE,
        .profile_dtb_path = "arch/orlix/boot/dts/enterprise.dtb",
        .kernel_cmdline = "console=ttyS0 root=/dev/ram0 rw orlix.profile=enterprise",
        .root_device = "/dev/ram0",
        .console_device = "ttyS0",
    },
};

static int OrlixBootStringIsPresent(const char *value)
{
    return value && value[0] != '\0';
}

static const struct OrlixProfileBootDefaults *OrlixProfileBootDefaultsFor(
    enum OrlixBootProfile profile)
{
    unsigned long index;

    for (index = 0; index < sizeof(OrlixProfileBootDefaultsTable) / sizeof(OrlixProfileBootDefaultsTable[0]); index++) {
        if (OrlixProfileBootDefaultsTable[index].profile == profile) {
            return &OrlixProfileBootDefaultsTable[index];
        }
    }

    return 0;
}

__attribute__((visibility("hidden"))) int OrlixPrepareBootInput(
    const struct OrlixBootConfig *config,
    struct OrlixBootInput *input)
{
    const struct OrlixProfileBootDefaults *defaults;

    if (!config || !input) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }
    if (!OrlixBootStringIsPresent(config->root_image_identifier)) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }
    if (!OrlixBootStringIsPresent(config->terminal_identifier)) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }

    defaults = OrlixProfileBootDefaultsFor(config->profile);
    if (!defaults) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }

    input->profile = config->profile;
    input->profile_dtb_path = defaults->profile_dtb_path;
    input->kernel_cmdline = defaults->kernel_cmdline;
    input->root_device = defaults->root_device;
    input->console_device = defaults->console_device;
    input->root_image_identifier = config->root_image_identifier;
    input->terminal_identifier = config->terminal_identifier;

    return ORLIX_BOOT_STATUS_OK;
}

__attribute__((visibility("hidden"))) int OrlixPrepareBootConfig(const struct OrlixBootConfig *config)
{
    struct OrlixBootInput input;

    return OrlixPrepareBootInput(config, &input);
}
```

- [ ] **Step 3: Update the public boot entrypoint to use private boot input preparation**

Replace `boot/loader.c` with:

```c
#include "boot/input.h"

int OrlixBoot(const struct OrlixBootConfig *config)
{
    struct OrlixBootInput input;

    if (OrlixPrepareBootInput(config, &input) != ORLIX_BOOT_STATUS_OK) {
        return ORLIX_BOOT_STATUS_INVALID_CONFIG;
    }

    (void)input;
    return ORLIX_BOOT_STATUS_UNAVAILABLE;
}
```

- [ ] **Step 4: Run the bootloader contract to verify the implementation passes**

Run:

```bash
rtk make test-bootloader-contract
```

Expected: PASS with output ending in `make: ok`.

- [ ] **Step 5: Commit private boot input preparation**

Run:

```bash
rtk git add boot/input.h boot/params.c boot/loader.c
rtk git commit -m "boot: prepare private Linux boot inputs"
```

## Task 3: Add A Failing Milestone 2 Shell Contract

**Files:**
- Create: `tests/milestone2_boot_contract.sh`
- Modify: `Makefile`

- [ ] **Step 1: Create the failing shell contract test**

Create `tests/milestone2_boot_contract.sh` with:

```bash
#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

MAKE_BIN="${MAKE_BIN:-make}"
PORT_DIR="Build/OrlixKernel/linux-6.12-port"

fail() {
    printf 'FAIL: %s\n' "$1" >&2
    exit 1
}

expect_file_contains() {
    local file="$1"
    local expected="$2"

    [ -f "$file" ] || fail "missing file: $file"
    grep -F "$expected" "$file" >/dev/null || fail "expected $file to contain: $expected"
}

expect_public_api_clean() {
    if grep -R "struct boot_params" OrlixKernel/include boot tests/bootloader_contract.c >/dev/null; then
        fail "raw boot_params must not be public bootloader API"
    fi
    if grep -R "OrlixPrepareBootParams" OrlixKernel/include boot tests/bootloader_contract.c >/dev/null; then
        fail "raw boot parameter preparation must not be public bootloader API"
    fi
}

"$MAKE_BIN" prepare-orlixkernel-port PROFILE=appstore >/dev/null

expect_file_contains "$PORT_DIR/arch/orlix/boot/dts/appstore.dts" 'compatible = "orlix,appstore", "orlix";'
expect_file_contains "$PORT_DIR/arch/orlix/boot/dts/appstore.dts" 'orlix.profile=appstore'
expect_file_contains "$PORT_DIR/arch/orlix/boot/dts/development.dts" 'compatible = "orlix,development", "orlix";'
expect_file_contains "$PORT_DIR/arch/orlix/boot/dts/development.dts" 'orlix.profile=development'
expect_file_contains "$PORT_DIR/arch/orlix/boot/dts/enterprise.dts" 'compatible = "orlix,enterprise", "orlix";'
expect_file_contains "$PORT_DIR/arch/orlix/boot/dts/enterprise.dts" 'orlix.profile=enterprise'

expect_public_api_clean
```

- [ ] **Step 2: Make the shell test executable**

Run:

```bash
chmod +x tests/milestone2_boot_contract.sh
```

Expected: no output and exit 0.

- [ ] **Step 3: Add the Makefile target**

Add `test-milestone2-boot-contract` to the `.PHONY` list in `Makefile`:

```make
.PHONY: bootstrap-linux-upstream validate-orlix-profile prepare-orlixkernel-port build-linux-kernel test-bootloader-contract test-milestone1-contract test-milestone2-boot-contract
```

Add this target after `test-milestone1-contract`:

```make
test-milestone2-boot-contract: test-bootloader-contract
	@MAKE_BIN="$(MAKE)" tests/milestone2_boot_contract.sh
```

- [ ] **Step 4: Run the shell contract test through Make**

Run:

```bash
rtk make test-milestone2-boot-contract
```

Expected: FAIL with output containing `missing file: Build/OrlixKernel/linux-6.12-port/arch/orlix/boot/dts/appstore.dts` because the durable profile DTS files do not exist yet.

- [ ] **Step 5: Commit the Milestone 2 shell contract**

Run:

```bash
rtk git add Makefile tests/milestone2_boot_contract.sh
rtk git commit -m "test: add milestone two boot contract"
```

## Task 4: Add Profile Device Tree Sources

**Files:**
- Create: `Linux/ports/orlix/overlay/arch/orlix/boot/dts/Makefile`
- Create: `Linux/ports/orlix/overlay/arch/orlix/boot/dts/appstore.dts`
- Create: `Linux/ports/orlix/overlay/arch/orlix/boot/dts/development.dts`
- Create: `Linux/ports/orlix/overlay/arch/orlix/boot/dts/enterprise.dts`

- [ ] **Step 1: Add the profile DTS Makefile**

Create `Linux/ports/orlix/overlay/arch/orlix/boot/dts/Makefile` with:

```make
dtb-y += appstore.dtb
dtb-y += development.dtb
dtb-y += enterprise.dtb
```

- [ ] **Step 2: Add the App Store profile DTS**

Create `Linux/ports/orlix/overlay/arch/orlix/boot/dts/appstore.dts` with:

```dts
/dts-v1/;

/ {
	compatible = "orlix,appstore", "orlix";
	#address-cells = <2>;
	#size-cells = <2>;

	chosen {
		bootargs = "console=ttyS0 root=/dev/ram0 rw orlix.profile=appstore";
		stdout-path = "serial0:115200n8";
	};

	memory@40000000 {
		device_type = "memory";
		reg = <0x0 0x40000000 0x0 0x10000000>;
	};

	aliases {
		serial0 = &serial0;
	};

	serial0: serial@10000000 {
		compatible = "ns16550a";
		reg = <0x0 0x10000000 0x0 0x1000>;
		clock-frequency = <1843200>;
		current-speed = <115200>;
		status = "okay";
	};
};
```

- [ ] **Step 3: Add the Development profile DTS**

Create `Linux/ports/orlix/overlay/arch/orlix/boot/dts/development.dts` with:

```dts
/dts-v1/;

/ {
	compatible = "orlix,development", "orlix";
	#address-cells = <2>;
	#size-cells = <2>;

	chosen {
		bootargs = "console=ttyS0 root=/dev/ram0 rw debug ignore_loglevel orlix.profile=development";
		stdout-path = "serial0:115200n8";
	};

	memory@40000000 {
		device_type = "memory";
		reg = <0x0 0x40000000 0x0 0x10000000>;
	};

	aliases {
		serial0 = &serial0;
	};

	serial0: serial@10000000 {
		compatible = "ns16550a";
		reg = <0x0 0x10000000 0x0 0x1000>;
		clock-frequency = <1843200>;
		current-speed = <115200>;
		status = "okay";
	};
};
```

- [ ] **Step 4: Add the Enterprise profile DTS**

Create `Linux/ports/orlix/overlay/arch/orlix/boot/dts/enterprise.dts` with:

```dts
/dts-v1/;

/ {
	compatible = "orlix,enterprise", "orlix";
	#address-cells = <2>;
	#size-cells = <2>;

	chosen {
		bootargs = "console=ttyS0 root=/dev/ram0 rw orlix.profile=enterprise";
		stdout-path = "serial0:115200n8";
	};

	memory@40000000 {
		device_type = "memory";
		reg = <0x0 0x40000000 0x0 0x10000000>;
	};

	aliases {
		serial0 = &serial0;
	};

	serial0: serial@10000000 {
		compatible = "ns16550a";
		reg = <0x0 0x10000000 0x0 0x1000>;
		clock-frequency = <1843200>;
		current-speed = <115200>;
		status = "okay";
	};
};
```

- [ ] **Step 5: Verify the DTS contract now passes**

Run:

```bash
rtk make test-milestone2-boot-contract
```

Expected: PASS with output ending in `make: ok`.

- [ ] **Step 6: Verify Kbuild still produces vmlinux**

Run:

```bash
rtk make build-linux-kernel PROFILE=appstore
```

Expected: PASS with output containing `Linux vmlinux ready:` and `Build/OrlixKernel/build/appstore/vmlinux`.

- [ ] **Step 7: Commit profile DTS sources**

Run:

```bash
rtk git add Linux/ports/orlix/overlay/arch/orlix/boot/dts
rtk git commit -m "linux: add Orlix profile device trees"
```

## Task 5: Document Milestone 2 Proof Boundary

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update the README with Milestone 2 proof commands**

In `README.md`, after the Milestone 1 proof command block, add:

````markdown
Milestone 2 boot-entrypoint proof is intentionally narrower than booting Linux. It verifies that the public API stays bootloader-shaped, that private boot input generation maps closed profiles to Linux-shaped defaults, and that durable profile DTS sources are materialized into the generated port tree:

```bash
make test-milestone2-boot-contract
```

Milestone 2 does not prove QEMU execution, iOS execution, task switching, MMU behavior, userspace access, device binding, or root filesystem assembly.
````

- [ ] **Step 2: Run the docs-adjacent contract checks**

Run:

```bash
rtk make test-milestone1-contract
rtk make test-milestone2-boot-contract
```

Expected: both commands pass with `make: ok`.

- [ ] **Step 3: Commit the README update**

Run:

```bash
rtk git add README.md
rtk git commit -m "docs: document milestone two boot proof"
```

## Task 6: Final Verification And Review

**Files:**
- No planned source edits.

- [ ] **Step 1: Run the required Milestone 1 proof commands**

Run:

```bash
rtk make prepare-orlixkernel-port PROFILE=appstore
rtk make build-linux-kernel PROFILE=appstore
rtk make build-linux-kernel PROFILE=development
```

Expected: the appstore build output contains `Linux vmlinux ready:` and `Build/OrlixKernel/build/appstore/vmlinux`; the development build output contains `Linux vmlinux ready:` and `Build/OrlixKernel/build/development/vmlinux`.

- [ ] **Step 2: Run the contract checks**

Run:

```bash
rtk make test-milestone1-contract
rtk make test-bootloader-contract
rtk make test-milestone2-boot-contract
```

Expected: all commands pass with `make: ok`.

- [ ] **Step 3: Run diff hygiene**

Run:

```bash
rtk git diff --check
```

Expected: no output and exit 0.

- [ ] **Step 4: Request spec compliance review**

Dispatch a reviewer with this scope:

```text
Review the Milestone 2 boot-entrypoint changes for spec compliance only. Confirm the public product API remains bootloader-shaped, raw Linux boot params are not exposed publicly, profile DTS files live under Linux/ports/orlix/overlay/arch/orlix/boot/dts, and no Linux subsystem behavior moved into forbidden local prototype paths.
```

Expected: no spec blockers.

- [ ] **Step 5: Request code quality review**

Dispatch a reviewer with this scope:

```text
Review the Milestone 2 boot-entrypoint changes for code quality and likely build/test correctness. Distinguish true blockers from acceptable proof scaffolding. Focus on boot/input.h, boot/params.c, boot/loader.c, tests, Makefile, and arch/orlix/boot/dts files.
```

Expected: no important issues requiring code changes before committing or opening a PR.

- [ ] **Step 6: Commit any review fixes or record no-fix result**

If reviewers find issues, apply only the targeted fixes, rerun the verification commands from Steps 1-3, then commit with a focused message. If reviewers find no required fixes, do not create an empty commit.

Expected final state: a branch with committed Milestone 2 boot-entrypoint proof changes and a clean worktree.
