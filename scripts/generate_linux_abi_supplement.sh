#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/linux-source" >&2
    exit 2
fi

linux_src=$1
src_file="$linux_src/include/linux/fs.h"
out_dir="third_party/linux-abi/6.12/arm64/include/linux"
out_file="$out_dir/umount.h"
commit=$(git -C "$linux_src" rev-parse HEAD)
tag=$(git -C "$linux_src" describe --tags --exact-match HEAD 2>/dev/null || true)

if [ ! -f "$src_file" ]; then
    echo "missing Linux source file: $src_file" >&2
    exit 1
fi

mkdir -p "$out_dir"

awk '
    /^#define[[:space:]]+(MNT_FORCE|MNT_DETACH|MNT_EXPIRE|UMOUNT_NOFOLLOW)[[:space:]]+/ {
        print
        seen[$2] = 1
    }
    END {
        if (!seen["MNT_FORCE"] || !seen["MNT_DETACH"] || !seen["MNT_EXPIRE"] || !seen["UMOUNT_NOFOLLOW"]) {
            exit 1
        }
    }
' "$src_file" > "$out_file.tmp.defines"

{
    echo "/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */"
    echo "/*"
    echo " * Generated Linux ABI supplement for IXLandSystem."
    echo " *"
    echo " * Source: Linux ${tag:-untagged} ${commit}"
    echo " * Source file: include/linux/fs.h"
    echo " * Generator: scripts/generate_linux_abi_supplement.sh"
    echo " *"
    echo " * Do not edit by hand. Regenerate from the matching Linux source tree."
    echo " */"
    echo "#ifndef _LINUX_UMOUNT_H"
    echo "#define _LINUX_UMOUNT_H"
    echo
    cat "$out_file.tmp.defines"
    echo
    echo "#endif /* _LINUX_UMOUNT_H */"
} > "$out_file"

rm "$out_file.tmp.defines"
