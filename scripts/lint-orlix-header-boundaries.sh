#!/usr/bin/env bash
set -euo pipefail

profile="${PROFILE:-appstore}"
archive_root=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        --profile)
            profile="$2"
            shift 2
            ;;
        --archive-root)
            archive_root="$2"
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

if [ -z "$archive_root" ]; then
    archive_root="Build/OrlixKernel/$profile"
fi

pattern='(/Applications/|/Library/Developer/CommandLineTools/SDKs/|/System/Library/Frameworks|/usr/include|/usr/local/include|/opt/homebrew/include|OrlixHostAdapter/Sources|OrlixKernel/Sources/include|OrlixKernel/Sources/boot|OrlixMLibC/Sources|Build/OrlixMLibC/sysroot|Foundation|Darwin|musl|glibc)'
found=0
failed=0

for slice in iphoneos iphonesimulator; do
    objects_dir="$archive_root/$slice/objects"
    if [ ! -d "$objects_dir" ]; then
        printf 'missing Linux object dependency directory: %s\n' "$objects_dir" >&2
        exit 1
    fi
    while IFS= read -r dep; do
        found=1
        if grep -nE "$pattern" "$dep"; then
            printf 'Linux object dependency includes forbidden host or libc boundary: %s\n' "$dep" >&2
            failed=1
        fi
    done < <(find "$objects_dir" -type f -name '*.d' -print)
done

if [ "$found" -eq 0 ]; then
    printf 'missing Linux object dependency files under %s\n' "$archive_root" >&2
    exit 1
fi

if [ "$failed" -ne 0 ]; then
    exit 1
fi

printf 'verified Orlix Linux object header boundaries: %s\n' "$archive_root"
