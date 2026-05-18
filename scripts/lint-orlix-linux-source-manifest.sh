#!/usr/bin/env bash
set -euo pipefail

profile="${PROFILE:-appstore}"
manifest=""
reasons="OrlixKernel/Sources/ports/orlix/macho-source-reasons.tsv"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --profile)
            profile="$2"
            shift 2
            ;;
        --manifest)
            manifest="$2"
            shift 2
            ;;
        --reasons)
            reasons="$2"
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

if [ -z "$manifest" ]; then
    manifest="Build/OrlixKernel/$profile/linux-object-manifest.txt"
fi

if [ ! -s "$manifest" ]; then
    printf 'missing non-empty Linux source manifest: %s\n' "$manifest" >&2
    exit 1
fi
if [ ! -s "$reasons" ]; then
    printf 'missing non-empty Linux source reason file: %s\n' "$reasons" >&2
    exit 1
fi

failed=0
while IFS= read -r source; do
    [ -n "$source" ] || continue
    rel="$(printf '%s\n' "$source" | sed -E 's#^(.*/)?Build/OrlixKernel/linux-[^/]+-port/##')"
    if [ "$rel" = "$source" ]; then
        printf 'manifest source is not from generated Orlix Linux port: %s\n' "$source" >&2
        failed=1
        continue
    fi
    if ! awk -F '\t' -v src="$rel" '$1 == src && $2 ~ /(blocker|reason)=/ { found = 1 } END { exit found ? 0 : 1 }' "$reasons"; then
        printf 'manifest source lacks named blocker or reason: %s\n' "$rel" >&2
        failed=1
    fi
done < "$manifest"

if [ "$failed" -ne 0 ]; then
    exit 1
fi

printf 'verified Orlix product Linux source manifest reasons: %s\n' "$manifest"
