#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
MAKE_BIN="${MAKE_BIN:-make}"

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

expect_success_not_contains() {
    local unexpected="$1"
    shift
    local output

    output="$({ "$@"; } 2>&1)"
    case "$output" in
        *"$unexpected"*)
            printf '%s\n' "$output" >&2
            fail "expected output not containing: $unexpected"
            ;;
    esac
}

expect_fail_contains "unsupported PROFILE=bogus" "$MAKE_BIN" validate-orlix-profile PROFILE=bogus
expect_success_contains "Build/OrlixKernel/linux-6.12-port" "$MAKE_BIN" -n prepare-orlixkernel-port PROFILE=appstore
expect_success_contains "Linux/ports/orlix/configs/appstore_defconfig" "$MAKE_BIN" -n prepare-orlixkernel-port PROFILE=appstore
expect_success_contains "Linux/ports/orlix/configs/appstore_defconfig" "$MAKE_BIN" -n prepare-orlixkernel-port PROFILE=appstore ORLIX_PROFILE_CONFIG=/tmp/bad
expect_success_not_contains "/tmp/bad" "$MAKE_BIN" -n prepare-orlixkernel-port PROFILE=appstore ORLIX_PROFILE_CONFIG=/tmp/bad
expect_success_contains "vmlinux" "$MAKE_BIN" -n build-linux-kernel PROFILE=appstore
expect_success_contains "Build/OrlixKernel/build/appstore" "$MAKE_BIN" -n build-linux-kernel PROFILE=appstore
expect_success_contains "Build/OrlixKernel/build/development" "$MAKE_BIN" -n build-linux-kernel PROFILE=development
expect_success_contains "expected_build_dir=\"$ROOT/Build/OrlixKernel/build/appstore\"" "$MAKE_BIN" -n build-linux-kernel PROFILE=appstore ORLIX_KERNEL_BUILD_ROOT=/tmp/orlix-build
expect_success_contains 'vmlinux="$build_dir/vmlinux"' "$MAKE_BIN" -n build-linux-kernel PROFILE=appstore ORLIX_KERNEL_VMLINUX=/tmp/bad-vmlinux
expect_success_not_contains "/tmp/bad-vmlinux" "$MAKE_BIN" -n build-linux-kernel PROFILE=appstore ORLIX_KERNEL_VMLINUX=/tmp/bad-vmlinux
expect_success_contains "refusing to use symlinked Build/OrlixKernel/build directory" "$MAKE_BIN" -n build-linux-kernel PROFILE=appstore
expect_success_contains "refusing to use symlinked Build/OrlixKernel/tool-shims directory" "$MAKE_BIN" -n build-linux-kernel PROFILE=appstore LINUX_SED=/tmp/gsed
expect_success_contains "refusing to use symlinked Build/OrlixKernel/tool-shims/appstore directory" "$MAKE_BIN" -n build-linux-kernel PROFILE=appstore LINUX_SED=/tmp/gsed
expect_success_contains 'ln -sf "$linux_sed" "$sed_shim_dir/sed"' "$MAKE_BIN" -n build-linux-kernel PROFILE=appstore LINUX_SED=/tmp/gsed
expect_success_contains "expected_boot_contract_dir=\"$ROOT/Build/OrlixKernel/bootloader-contract\"" "$MAKE_BIN" -n test-bootloader-contract ORLIX_BOOT_CONTRACT_DIR=/tmp/boot-contract
expect_success_contains "refusing to use symlinked Build directory" "$MAKE_BIN" -n test-bootloader-contract
expect_success_contains "refusing to use symlinked Build/OrlixKernel directory" "$MAKE_BIN" -n test-bootloader-contract
expect_success_contains "refusing to use symlinked Build/OrlixKernel/bootloader-contract directory" "$MAKE_BIN" -n test-bootloader-contract
expect_success_not_contains "/tmp/boot-contract" "$MAKE_BIN" -n test-bootloader-contract ORLIX_BOOT_CONTRACT_DIR=/tmp/boot-contract

expect_fail_contains "No rule to make target" "$MAKE_BIN" -n prepare-linux-worktree
expect_fail_contains "No rule to make target" "$MAKE_BIN" -n build-linux-orlix-kernel-simulator
expect_fail_contains "No rule to make target" "$MAKE_BIN" -n build-linux-simulator
expect_fail_contains "No rule to make target" "$MAKE_BIN" -n build-linux-iphoneos
expect_fail_contains "No rule to make target" "$MAKE_BIN" -n package-orlixkernel-xcframework

if [ -e Linux/ports/orlix/overlay/arch/orlix/configs/defconfig ]; then
    fail "profile defconfig must not be committed under the arch overlay"
fi
