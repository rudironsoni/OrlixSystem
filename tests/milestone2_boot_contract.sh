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
