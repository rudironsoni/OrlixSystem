#!/usr/bin/env bash
set -euo pipefail

project_file=${1:-project.yml}

linux_version=$(sed -n "s/.*LINUX_VERSION:[[:space:]]*'\\{0,1\\}\\([^'[:space:]]*\\)'\\{0,1\\}.*/\\1/p" "$project_file" | head -n1)
linux_arch=$(sed -n "s/.*LINUX_ARCH:[[:space:]]*'\\{0,1\\}\\([^'[:space:]]*\\)'\\{0,1\\}.*/\\1/p" "$project_file" | head -n1)

if [ -z "$linux_version" ] || [ -z "$linux_arch" ]; then
    echo "unable to determine LINUX_VERSION/LINUX_ARCH from $project_file" >&2
    exit 1
fi

tuple_root="third_party/linux/${linux_version}/${linux_arch}"
uapi_root="${tuple_root}/uapi/include"
srctree_root="${tuple_root}/srctree"
objtree_root="${tuple_root}/objtree"

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

require_nonempty_dir() {
    local path=$1
    require_dir "$path"
    if ! find "$path" -type f -print -quit 2>/dev/null | grep -q .; then
        echo "missing required files under: $path" >&2
        exit 1
    fi
}

echo "=== Linux vendor headers layout ==="
echo "rationale: Linux-facing contracts must come from a single tuple root and must not mix old category layouts."

echo ""
echo "=== Check A: Forbidden ABI directory ==="
echo "rationale: vendored Linux headers must not grow an 'abi' taxonomy; the tuple surfaces are uapi/srctree/objtree only."
ABI_DIR=$(find third_party/linux -type d -name abi -print -quit 2>/dev/null || true)
if [ -n "${ABI_DIR:-}" ]; then
    echo "FAIL: forbidden directory exists under third_party/linux: $ABI_DIR" >&2
    exit 1
fi
echo "   ✓ No third_party/linux/**/abi directories exist"

echo ""
echo "=== Check B: Old vendor roots are forbidden ==="
echo "rationale: the old roots (third_party/linux/{uapi,kheaders}/<ver>/<arch>) must not coexist with the tuple layout; this prevents accidental dual-source truth."
OLD_ROOTS=$(
    find third_party/linux -type d \( \
        -path 'third_party/linux/uapi/*/*' -o \
        -path 'third_party/linux/kheaders/*/*' \
    \) -print 2>/dev/null || true
)
if [ -n "${OLD_ROOTS:-}" ] || [ -d third_party/linux/uapi ] || [ -d third_party/linux/kheaders ]; then
    echo "FAIL: old vendor roots detected under third_party/linux (remove uapi/<ver>/<arch> and kheaders/<ver>/<arch> layouts):" >&2
    if [ -n "${OLD_ROOTS:-}" ]; then
        echo "$OLD_ROOTS" >&2
    else
        # Still fail if the old category roots exist even without <ver>/<arch> children.
        find third_party/linux -maxdepth 2 -type d \( -name uapi -o -name kheaders \) -print 2>/dev/null >&2 || true
    fi
    exit 1
fi
echo "   ✓ No old uapi/kheaders category roots exist"

echo ""
echo "=== Check C: Required tuple layout exists ==="
echo "rationale: vendored Linux headers must be laid out as third_party/linux/<ver>/<arch>/{uapi,srctree,objtree} with the expected Linux include subtrees."
require_nonempty_dir "$uapi_root"
require_nonempty_dir "${srctree_root}/include"
require_nonempty_dir "${srctree_root}/arch/${linux_arch}/include"
require_nonempty_dir "${objtree_root}/include/generated"
require_nonempty_dir "${objtree_root}/include/config"
require_nonempty_dir "${objtree_root}/arch/${linux_arch}/include/generated"

echo ""
echo "=== Check D: Expected marker headers exist ==="
echo "rationale: sanity-check that the tuple root contains real UAPI + kernel-internal header surfaces for the configured version/arch."
require_file "$uapi_root/linux/wait.h"
require_file "$uapi_root/asm/signal.h"
require_file "$uapi_root/asm-generic/errno-base.h"
require_file "$uapi_root/linux/futex.h"
require_file "$uapi_root/linux/seccomp.h"
require_file "${srctree_root}/include/linux/fs.h"
require_file "${srctree_root}/include/linux/sched.h"
require_file "${srctree_root}/arch/${linux_arch}/include/asm/unistd.h"
require_file "${objtree_root}/include/generated/autoconf.h"
require_file "${objtree_root}/include/generated/utsrelease.h"
require_file "${tuple_root}/README.md"
require_file "${tuple_root}/source.json"
require_file "${tuple_root}/manifest.sha256"
echo "   ✓ Tuple-root sanity markers exist"

echo ""
echo "=== Linux vendor headers are generated artifacts ==="
echo "rationale: vendored headers must be outputs only; generator codepaths must not leak into the artifact tree."
GENERATOR_STRINGS=$(rtk rg -n 'generate_linux_abi_supplement|generate_syscall_gap_matrix' third_party/linux 2>/dev/null || true)
if [ -n "$GENERATOR_STRINGS" ]; then
    echo "FAIL: removed generators are still referenced in third_party/linux artifacts:"
    echo "$GENERATOR_STRINGS"
    exit 1
fi
echo "   ✓ No stale generator references remain in vendored headers"
