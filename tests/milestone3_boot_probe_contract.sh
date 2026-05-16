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

expect_file_not_contains() {
    local file="$1"
    local unexpected="$2"

    [ -f "$file" ] || fail "missing file: $file"
    if grep -F "$unexpected" "$file" >/dev/null; then
        fail "expected $file not to contain: $unexpected"
    fi
}

expect_profile_probe_shape() {
    local profile="$1"
    local dts="$PORT_DIR/arch/orlix/boot/dts/$profile.dts"

    expect_file_contains "$dts" 'virtio_base: virtio@10001000 {'
    expect_file_contains "$dts" 'virtio_state: virtio@10001200 {'
    expect_file_contains "$dts" 'compatible = "virtio,mmio";'
    expect_file_contains "$dts" 'reg = <0x0 0x10001000 0x0 0x200>;'
    expect_file_contains "$dts" 'reg = <0x0 0x10001200 0x0 0x200>;'
    expect_file_contains "$dts" 'interrupts = <32>;'
    expect_file_contains "$dts" 'interrupts = <33>;'
}

expect_profile_config() {
    local config="$1"

    expect_file_contains "$config" 'CONFIG_OF=y'
    expect_file_contains "$config" 'CONFIG_VIRTIO=y'
    expect_file_contains "$config" 'CONFIG_VIRTIO_MMIO=y'
    expect_file_contains "$config" 'CONFIG_VIRTIO_BLK=y'
    expect_file_not_contains "$config" 'CONFIG_ORLIX_BLOCK=y'
}

"$MAKE_BIN" prepare-orlixkernel-port PROFILE=appstore >/dev/null

expect_profile_probe_shape appstore
expect_profile_probe_shape development
expect_profile_probe_shape enterprise

expect_profile_config Linux/ports/orlix/configs/appstore_defconfig
expect_profile_config Linux/ports/orlix/configs/development_defconfig
expect_profile_config Linux/ports/orlix/configs/enterprise_defconfig

expect_file_contains "$PORT_DIR/arch/orlix/Kconfig" 'select OF'
expect_file_contains "$PORT_DIR/arch/orlix/Kconfig" 'select HAS_IOMEM'
expect_file_contains "$PORT_DIR/arch/orlix/Kconfig" 'select HAS_DMA'

if grep -R 'OrlixBootHandoff' OrlixKernel/include >/dev/null; then
    fail 'private boot handoff must not be public product API'
fi
