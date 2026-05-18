#!/usr/bin/env bash
set -euo pipefail

profile="${PROFILE:-appstore}"
headers=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        --profile)
            profile="$2"
            shift 2
            ;;
        --headers)
            headers="$2"
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

if [ -z "$headers" ]; then
    headers="Build/OrlixMLibC/kernel-headers/$profile/include"
fi

if [ ! -d "$headers" ]; then
    printf 'missing installed UAPI headers: %s\n' "$headers" >&2
    exit 1
fi

failed=0
if [ -e "$headers/asm/compiler.h" ]; then
    printf 'installed UAPI headers must not contain asm/compiler.h: %s\n' "$headers/asm/compiler.h" >&2
    failed=1
fi

pattern='__MACH__|__ORLIX|__DISABLE_EXPORTS|HAVE_ARCH_COMPILER_H|asm/compiler\.h|OrlixHostAdapter|<Foundation/|Foundation[.]framework|<Darwin/|Darwin[.]framework|/Darwin/'
if grep -RInE "$pattern" "$headers"; then
    printf 'installed UAPI headers contain Orlix Mach-O or host policy leakage: %s\n' "$headers" >&2
    failed=1
fi

if [ "$failed" -ne 0 ]; then
    exit 1
fi

printf 'verified Orlix installed UAPI header boundaries: %s\n' "$headers"
