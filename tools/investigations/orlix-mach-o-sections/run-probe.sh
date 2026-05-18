#!/usr/bin/env bash
set -euo pipefail

profile="${PROFILE:-appstore}"
cc="${ORLIX_MACHO_CC:-clang}"
otool_cmd="${ORLIX_MACHO_OTOOL:-otool}"
target="${ORLIX_IOS_SIMULATOR_TARGET:-arm64-apple-ios-simulator}"
output=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        --profile)
            profile="$2"
            shift 2
            ;;
        --cc)
            cc="$2"
            shift 2
            ;;
        --otool)
            otool_cmd="$2"
            shift 2
            ;;
        --target)
            target="$2"
            shift 2
            ;;
        --output)
            output="$2"
            shift 2
            ;;
        *)
            printf 'unknown argument: %s\n' "$1" >&2
            exit 1
            ;;
    esac
done

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "$script_dir/../../.." && pwd -P)"
cd "$repo_root"

if [ -z "$output" ]; then
    output="Build/OrlixKernel/probes/$profile/orlix-mach-o-sections"
fi

case "$output" in
    "$repo_root"/Build/OrlixKernel/probes/*|Build/OrlixKernel/probes/*) ;;
    *)
        printf 'refusing to write Mach-O section probe outside Build/OrlixKernel/probes: %s\n' "$output" >&2
        exit 1
        ;;
esac

for path in Build Build/OrlixKernel Build/OrlixKernel/probes "$output"; do
    if [ -L "$path" ]; then
        printf 'refusing to use symlinked probe path: %s\n' "$path" >&2
        exit 1
    fi
done

command -v "$cc" >/dev/null 2>&1 || { printf 'missing compiler: %s\n' "$cc" >&2; exit 1; }
command -v "$otool_cmd" >/dev/null 2>&1 || { printf 'missing otool: %s\n' "$otool_cmd" >&2; exit 1; }

mkdir -p "$output"
object="$output/probe_sections.o"
sections="$output/sections.txt"

/usr/bin/env -u SDKROOT "$cc" -target "$target" -isysroot / \
    -ffreestanding -fno-builtin -fno-stack-protector -fno-objc-arc \
    -nostdinc -c "$script_dir/probe_sections.c" -o "$object"

"$otool_cmd" -l "$object" > "$sections"

for expected in \
    'sectname __init' \
    'sectname __initdata' \
    'sectname __percpu' \
    'sectname __roinit' \
    'sectname __discard'; do
    if ! grep -q "$expected" "$sections"; then
        printf 'Mach-O section probe missing %s in %s\n' "$expected" "$object" >&2
        exit 1
    fi
done

printf 'verified probe-only Mach-O section carrier: %s\n' "$object"
