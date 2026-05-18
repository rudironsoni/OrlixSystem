#!/usr/bin/env bash
set -euo pipefail

project="OrlixSystem.xcodeproj"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --project)
            project="$2"
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

if [ ! -d "$project" ]; then
    printf 'missing generated Xcode project: %s\n' "$project" >&2
    exit 1
fi

failed=0
check_absent() {
    local description="$1"
    local pattern="$2"
    if grep -RInE "$pattern" "$project"; then
        printf 'generated Xcode project violates boundary: %s\n' "$description" >&2
        failed=1
    fi
}

check_absent 'upstream Linux source' 'OrlixKernel/Sources/upstream/linux-[0-9]'
check_absent 'generated Linux port source' 'Build/OrlixKernel/linux-[^/]+-port'
check_absent 'Linux arch overlay C source' 'OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/.*[.]c'
check_absent 'Linux Orlix driver overlay C source' 'OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix/.*[.]c'
check_absent 'Linux Kbuild output' 'Build/OrlixKernel/build'
check_absent 'Mach-O Linux object files' 'Build/OrlixKernel/.*/objects'
check_absent 'OrlixKernel probe outputs' 'Build/OrlixKernel/probes'
check_absent 'installed OrlixMLibC UAPI headers' 'Build/OrlixMLibC/kernel-headers'

if [ "$failed" -ne 0 ]; then
    exit 1
fi

printf 'verified generated Xcode project Linux boundary: %s\n' "$project"
