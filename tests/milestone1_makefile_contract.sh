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
expect_success_contains "expected_build_dir=\"$ROOT/Build/OrlixKernel/build/appstore\"" make -n build-linux-kernel PROFILE=appstore ORLIX_KERNEL_BUILD_ROOT=/tmp/orlix-build
expect_success_contains "refusing to use symlinked Build/OrlixKernel/build directory" make -n build-linux-kernel PROFILE=appstore
expect_success_contains 'ln -sf "$linux_sed" "$sed_shim_dir/sed"' make -n build-linux-kernel PROFILE=appstore LINUX_SED=/tmp/gsed

expect_fail_contains "No rule to make target" make -n prepare-linux-worktree
expect_fail_contains "No rule to make target" make -n build-linux-orlix-kernel-simulator
expect_fail_contains "No rule to make target" make -n build-linux-simulator
expect_fail_contains "No rule to make target" make -n build-linux-iphoneos
expect_fail_contains "No rule to make target" make -n package-orlixkernel-xcframework

if [ -e Linux/ports/orlix/overlay/arch/orlix/configs/defconfig ]; then
    fail "profile defconfig must not be committed under the arch overlay"
fi
