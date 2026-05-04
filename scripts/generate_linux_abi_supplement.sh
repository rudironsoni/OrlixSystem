#!/usr/bin/env bash
set -euo pipefail

# mlibc is a design reference for the native iOS arm64/aarch64 userspace path:
# - mlibc/abis/linux is used here as a surface coverage manifest.
# - mlibc/sysdeps/linux and mlibc/options are libc implementation references.
#   They belong in IXLandMLibC, not in IXLandSystem.
# This script only generates IXLandSystem-owned Linux-source ABI supplements
# that are missing from vendored Linux UAPI and needed by kernel/runtime code.

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/linux-source" >&2
    exit 2
fi

linux_src=$1
abi_version=6.12
abi_arch=arm64
out_root="third_party/linux/abi/${abi_version}/${abi_arch}/include"
commit=$(git -C "$linux_src" rev-parse HEAD)
tag=$(git -C "$linux_src" describe --tags --exact-match HEAD 2>/dev/null || true)
generated_files=()
mlibc_linux_surface_headers=(
    access.h
    auxv.h
    blkcnt_t.h
    blksize_t.h
    clockid_t.h
    dev_t.h
    epoll.h
    errno.h
    fcntl.h
    fsblkcnt_t.h
    fsfilcnt_t.h
    gid_t.h
    in.h
    ino_t.h
    inotify.h
    ioctls.h
    ipc.h
    limits.h
    mode_t.h
    mqueue.h
    msg.h
    nlink_t.h
    packet.h
    pid_t.h
    poll.h
    ptrace.h
    random.h
    reboot.h
    resource.h
    rlim_t.h
    seek-whence.h
    shm.h
    sigevent.h
    signal.h
    sigval.h
    socket.h
    socklen_t.h
    stat.h
    statfs.h
    statvfs.h
    statx.h
    suseconds_t.h
    termios.h
    time.h
    uid_t.h
    utsname.h
    vm-flags.h
    vt.h
    wait.h
    xattr.h
)
uapi_surface_specs=(
    "access.h|linux/fcntl.h"
    "auxv.h|linux/auxvec.h"
    "epoll.h|linux/eventpoll.h"
    "errno.h|asm-generic/errno.h"
    "fcntl.h|linux/fcntl.h"
    "in.h|linux/in.h"
    "inotify.h|linux/inotify.h"
    "ioctls.h|asm/ioctls.h"
    "ipc.h|linux/ipc.h"
    "limits.h|linux/limits.h"
    "mqueue.h|linux/mqueue.h"
    "msg.h|linux/msg.h"
    "packet.h|linux/if_packet.h"
    "poll.h|linux/poll.h"
    "ptrace.h|linux/ptrace.h"
    "random.h|linux/random.h"
    "reboot.h|linux/reboot.h"
    "resource.h|asm-generic/resource.h"
    "seek-whence.h|linux/fs.h"
    "shm.h|linux/shm.h"
    "signal.h|linux/signal.h"
    "socket.h|linux/socket.h"
    "stat.h|linux/stat.h"
    "statx.h|linux/stat.h"
    "termios.h|asm/termbits.h"
    "time.h|linux/time_types.h"
    "utsname.h|linux/utsname.h"
    "vt.h|linux/vt.h"
    "wait.h|linux/wait.h"
    "xattr.h|linux/xattr.h"
)
generated_surface_specs=(
    "statfs.h|linux/statfs.h"
)
libc_owned_surface_headers=(
    blkcnt_t.h
    blksize_t.h
    clockid_t.h
    dev_t.h
    fsblkcnt_t.h
    fsfilcnt_t.h
    gid_t.h
    ino_t.h
    mode_t.h
    nlink_t.h
    pid_t.h
    rlim_t.h
    sigevent.h
    sigval.h
    socklen_t.h
    statvfs.h
    suseconds_t.h
    uid_t.h
)
deferred_surface_headers=(
    vm-flags.h
)
linux_source_abi_specs=(
    "linux/umount.h|_LINUX_UMOUNT_H|include/linux/fs.h|MNT_FORCE MNT_DETACH MNT_EXPIRE UMOUNT_NOFOLLOW UMOUNT_UNUSED"
    "linux/statfs.h|_LINUX_STATFS_H|include/linux/statfs.h|ST_RDONLY ST_NOSUID ST_NODEV ST_NOEXEC ST_SYNCHRONOUS ST_VALID ST_MANDLOCK ST_NOATIME ST_NODIRATIME ST_RELATIME ST_NOSYMFOLLOW"
)

require_source_file() {
    local source_file=$1

    if [ ! -f "$source_file" ]; then
        echo "missing Linux source file: $source_file" >&2
        exit 1
    fi
}

array_contains() {
    local needle=$1
    shift

    for item in "$@"; do
        if [ "$item" = "$needle" ]; then
            return 0
        fi
    done
    return 1
}

spec_value_for_key() {
    local key=$1
    shift
    local spec
    local spec_key
    local spec_value

    for spec in "$@"; do
        IFS='|' read -r spec_key spec_value <<< "$spec"
        if [ "$spec_key" = "$key" ]; then
            printf '%s\n' "$spec_value"
            return 0
        fi
    done
    return 1
}

validate_mlibc_surface_manifest() {
    local header
    local mapped_path
    local missing=0
    local uapi_root="third_party/linux/uapi/${abi_version}/${abi_arch}/include"

    for header in "${mlibc_linux_surface_headers[@]}"; do
        if mapped_path=$(spec_value_for_key "$header" "${uapi_surface_specs[@]}"); then
            if [ ! -f "${uapi_root}/${mapped_path}" ]; then
                echo "mlibc Linux surface maps ${header} to missing vendored UAPI ${mapped_path}" >&2
                missing=1
            fi
            continue;
        fi
        if spec_value_for_key "$header" "${generated_surface_specs[@]}" >/dev/null; then
            continue
        fi
        if array_contains "$header" "${libc_owned_surface_headers[@]}"; then
            continue
        fi
        if array_contains "$header" "${deferred_surface_headers[@]}"; then
            continue
        fi

        echo "mlibc Linux surface header is unclassified: ${header}" >&2
        missing=1
    done

    if [ "$missing" -ne 0 ]; then
        exit 1
    fi
}

write_header() {
    local relative_path=$1
    local include_guard=$2
    local source_label=$3
    local defines_file=$4
    local out_file="${out_root}/${relative_path}"

    mkdir -p "$(dirname "$out_file")"
    {
        echo "/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */"
        echo "/*"
        echo " * Generated Linux ABI supplement for IXLandSystem."
        echo " *"
        echo " * Source: Linux ${tag:-untagged} ${commit}"
        echo " * Source file: ${source_label}"
        echo " * Generator: scripts/generate_linux_abi_supplement.sh"
        echo " *"
        echo " * Do not edit by hand. Regenerate from the matching Linux source tree."
        echo " */"
        echo "#ifndef ${include_guard}"
        echo "#define ${include_guard}"
        echo
        cat "$defines_file"
        echo
        echo "#endif /* ${include_guard} */"
    } > "$out_file"
    generated_files+=("$relative_path")
}

generate_header_from_spec() {
    local spec=$1
    local relative_path
    local include_guard
    local source_rel
    local source_file
    local tmp_file
    local defines
    local define_regex

    IFS='|' read -r relative_path include_guard source_rel defines <<< "$spec"
    source_file="${linux_src}/${source_rel}"
    require_source_file "$source_file"
    tmp_file=$(mktemp)
    define_regex=$(printf '%s\n' $defines | paste -sd'|' -)

    awk -v names="$defines" -v define_regex="$define_regex" '
        BEGIN {
            count = split(names, required, /[[:space:]]+/)
            regex = "^#define[[:space:]]+(" define_regex ")[[:space:]]+"
        }
        $0 ~ regex {
            print
            seen[$2] = 1
        }
        END {
            for (i = 1; i <= count; i++) {
                if (!seen[required[i]]) {
                    exit 1
                }
            }
        }
    ' "$source_file" > "$tmp_file"
    write_header "$relative_path" "$include_guard" "$source_rel" "$tmp_file"
    rm "$tmp_file"
}

reset_generated_tree() {
    if [ -e "$out_root" ] && [ ! -d "$out_root" ]; then
        echo "ABI include output is not a directory: $out_root" >&2
        exit 1
    fi

    mkdir -p "$out_root"
    find "$out_root" -type f -delete
    find "$out_root" -depth -type d -empty -delete
    mkdir -p "$out_root"
}

reset_generated_tree

validate_mlibc_surface_manifest

for spec in "${linux_source_abi_specs[@]}"; do
    generate_header_from_spec "$spec"
done

for file in "${generated_files[@]}"; do
    echo "${out_root}/${file}"
done
