#!/usr/bin/env bash
set -euo pipefail

profile="${PROFILE:-appstore}"
archive_root=""
otool_cmd="${ORLIX_MACHO_OTOOL:-otool}"

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
        --otool)
            otool_cmd="$2"
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

command -v "$otool_cmd" >/dev/null 2>&1 || { printf 'missing otool: %s\n' "$otool_cmd" >&2; exit 1; }

found=0
failed=0
for slice in iphoneos iphonesimulator; do
    objects_dir="$archive_root/$slice/objects"
    if [ ! -d "$objects_dir" ]; then
        printf 'missing Mach-O object directory: %s\n' "$objects_dir" >&2
        exit 1
    fi
    while IFS= read -r object; do
        found=1
        if ! "$otool_cmd" -l "$object" | awk -v file="$object" '
            /sectname / { sect = $2; next }
            /segname / && sect != "" {
                printf "%s: %s,%s\n", file, $2, sect
                if ($2 == "__ORLIX") bad = 1
                sect = ""
                next
            }
            END { exit bad ? 1 : 0 }
        '; then
            printf 'product object contains unallowlisted __ORLIX section: %s\n' "$object" >&2
            failed=1
        fi
    done < <(find "$objects_dir" -type f -name '*.o' -print)
done

if [ "$found" -eq 0 ]; then
    printf 'missing Mach-O product objects under %s\n' "$archive_root" >&2
    exit 1
fi

if [ "$failed" -ne 0 ]; then
    exit 1
fi

printf 'verified Orlix Mach-O product section allowlist: %s\n' "$archive_root"
