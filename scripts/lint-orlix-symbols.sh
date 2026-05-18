#!/usr/bin/env bash
set -euo pipefail

framework_binary=".deriveddata/OrlixSystem-sim/Build/Products/Debug-iphonesimulator/OrlixKernel.framework/OrlixKernel"
nm_cmd="${ORLIX_MACHO_NM:-nm}"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --framework-binary)
            framework_binary="$2"
            shift 2
            ;;
        --nm)
            nm_cmd="$2"
            shift 2
            ;;
        *)
            printf 'unknown argument: %s\n' "$1" >&2
            exit 1
            ;;
    esac
done

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "$script_dir/.." && pwd -P)"
cd "$repo_root"

command -v "$nm_cmd" >/dev/null 2>&1 || { printf 'missing nm: %s\n' "$nm_cmd" >&2; exit 1; }

failed=0
fake_pattern='(^|[[:space:]])((asmlinkage|static|__visible)[[:space:]]+)*(void|int)[[:space:]]+_?start_kernel[[:space:]]*[(]|^[[:space:]]*#define[[:space:]]+_?start_kernel\b'
for root in \
    OrlixKernel/Sources/ports/orlix/overlay \
    OrlixKernel/Sources/boot \
    OrlixHostAdapter/Sources; do
    if [ -d "$root" ] && grep -RInE "$fake_pattern" "$root"; then
        printf 'found fake or local start_kernel provider under %s\n' "$root" >&2
        failed=1
    fi
done

if [ ! -s "$framework_binary" ]; then
    printf 'missing OrlixKernel framework binary: %s\n' "$framework_binary" >&2
    exit 1
fi

symbols="$(mktemp)"
trap 'rm -f "$symbols"' EXIT
"$nm_cmd" -gU "$framework_binary" > "$symbols"

if grep -q '[[:space:]]_start_kernel$' "$symbols"; then
    if [ "${ORLIX_ALLOW_REAL_START_KERNEL:-0}" != "1" ]; then
        printf 'OrlixKernel.framework contains _start_kernel before real upstream product link is accepted\n' >&2
        failed=1
    fi
fi

if grep -q '[[:space:]]_arch_boot_entry$' "$symbols" && grep -q '[[:space:]]_arch_boot_params$' "$symbols"; then
    :
else
    printf 'OrlixKernel.framework is missing expected arch boot symbols\n' >&2
    failed=1
fi

if [ "$failed" -ne 0 ]; then
    exit 1
fi

printf 'verified OrlixKernel product symbol boundary: %s\n' "$framework_binary"
