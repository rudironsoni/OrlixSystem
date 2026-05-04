#!/usr/bin/env bash
set -euo pipefail

project_file=${1:-project.yml}

linux_version=$(sed -n "s/.*LINUX_UAPI_VERSION: '\([^']*\)'.*/\1/p" "$project_file" | head -n1)
linux_arch=$(sed -n "s/.*LINUX_UAPI_ARCH: \([^[:space:]]*\).*/\1/p" "$project_file" | head -n1)

if [ -z "$linux_version" ] || [ -z "$linux_arch" ]; then
    echo "unable to determine LINUX_UAPI_VERSION/LINUX_UAPI_ARCH from $project_file" >&2
    exit 1
fi

uapi_root="third_party/linux/uapi/${linux_version}/${linux_arch}/include"
kheaders_root="third_party/linux/kheaders/${linux_version}/${linux_arch}"
abi_root="third_party/linux/abi/${linux_version}/${linux_arch}/include"

require_file() {
    local path=$1
    if [ ! -f "$path" ]; then
        echo "missing required file: $path" >&2
        exit 1
    fi
}

require_dir() {
    local path=$1
    if [ ! -d "$path" ]; then
        echo "missing required directory: $path" >&2
        exit 1
    fi
}

echo "=== Linux vendor headers layout ==="
require_file "$uapi_root/linux/wait.h"
require_file "$uapi_root/asm/signal.h"
require_file "$uapi_root/asm-generic/errno-base.h"
require_file "$uapi_root/linux/futex.h"
require_file "$uapi_root/linux/seccomp.h"
require_file "$kheaders_root/srctree/include/linux/fs.h"
require_file "$kheaders_root/srctree/include/linux/sched.h"
require_file "$kheaders_root/srctree/arch/${linux_arch}/include/asm/unistd.h"
require_file "$kheaders_root/objtree/include/generated/autoconf.h"
require_file "$kheaders_root/objtree/include/generated/utsrelease.h"
require_dir "$kheaders_root/objtree/arch/${linux_arch}/include/generated/asm"
if ! find "$kheaders_root/objtree/arch/${linux_arch}/include/generated/asm" -type f | grep -q .; then
    echo "missing generated arch asm headers under $kheaders_root/objtree/arch/${linux_arch}/include/generated/asm" >&2
    exit 1
fi
require_file "$abi_root/linux/statfs.h"
require_file "$abi_root/linux/umount.h"
require_file "$kheaders_root/include-paths.mk"
require_file "$kheaders_root/include-paths.xcconfig"
require_file "$kheaders_root/provenance.json"
require_file "$kheaders_root/manifest.sha256"
echo "   ✓ Generated UAPI, kheaders, and ABI roots exist"

echo ""
echo "=== Linux vendor headers are generated artifacts ==="
GENERATOR_STRINGS=$(rtk rg -n 'generate_linux_abi_supplement|generate_syscall_gap_matrix' third_party/linux 2>/dev/null || true)
if [ -n "$GENERATOR_STRINGS" ]; then
    echo "FAIL: removed generators are still referenced in third_party/linux artifacts:"
    echo "$GENERATOR_STRINGS"
    exit 1
fi
echo "   ✓ No stale generator references remain in vendored headers"
