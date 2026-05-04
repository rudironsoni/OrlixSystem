#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

linux_version=${1:-${LINUX_VERSION:-}}
linux_arch=${2:-${LINUX_ARCH:-}}
kernel_series=${LINUX_KERNEL_SERIES:-}
vendor_root=${LINUX_VENDOR_ROOT:-third_party/linux}
keep_tmp=${KEEP_LINUX_TMP:-0}
linux_make=${LINUX_MAKE:-}
llvm_bin=${LINUX_LLVM_BIN:-}
linux_sed=${LINUX_SED:-}

if [ -z "$linux_version" ] || [ -z "$linux_arch" ]; then
    echo "usage: $0 <linux-version> <linux-arch>" >&2
    echo "or set LINUX_VERSION and LINUX_ARCH in the environment" >&2
    exit 2
fi

case "$linux_arch" in
    arm64)
        ;;
    *)
        echo "unsupported Linux arch: $linux_arch" >&2
        exit 1
        ;;
esac

if [ -z "$kernel_series" ]; then
    kernel_series="v${linux_version%%.*}.x"
fi

if [ -z "$llvm_bin" ] && [ -d /opt/homebrew/opt/llvm/bin ]; then
    llvm_bin=/opt/homebrew/opt/llvm/bin
fi

if [ -n "$llvm_bin" ]; then
    export PATH="$llvm_bin:$PATH"
fi

if [ -z "$linux_sed" ] && command -v gsed >/dev/null 2>&1; then
    linux_sed=$(command -v gsed)
fi

if [ -z "$linux_make" ]; then
    if command -v gmake >/dev/null 2>&1; then
        linux_make=$(command -v gmake)
    else
        linux_make=$(command -v make)
    fi
fi

if ! "$linux_make" --version 2>/dev/null | head -n1 | grep -Eq 'GNU Make ([4-9]|[1-9][0-9])'; then
    echo "Linux vendoring requires GNU Make >= 4.0; found: $("$linux_make" --version | head -n1)" >&2
    echo "Set LINUX_MAKE=/path/to/gmake and rerun." >&2
    exit 1
fi

if ! command -v clang >/dev/null 2>&1 || ! command -v ld.lld >/dev/null 2>&1; then
    echo "Linux vendoring requires clang and ld.lld in PATH." >&2
    echo "Set LINUX_LLVM_BIN=/path/to/llvm/bin and rerun." >&2
    exit 1
fi

tarball_url=${LINUX_TARBALL_URL:-"https://cdn.kernel.org/pub/linux/kernel/${kernel_series}/linux-${linux_version}.tar.xz"}

if [[ "$vendor_root" = /* ]]; then
    final_vendor_root=$vendor_root
else
    final_vendor_root="$repo_root/$vendor_root"
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/vendor-linux-headers.XXXXXX")

cleanup() {
    if [ "$keep_tmp" = "1" ]; then
        echo "kept temporary directory: $tmp"
        return
    fi
    rm -rf "$tmp"
}
trap cleanup EXIT

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

copy_tree() {
    local src=$1
    local dest=$2
    require_dir "$src"
    mkdir -p "$dest"
    cp -R "$src/." "$dest"
}

write_umount_abi_header() {
    local source_file=$1
    local out_file=$2
    local tmp_defs="$tmp/umount-defines.h"

    awk '
        /^#define[[:space:]]+(MNT_FORCE|MNT_DETACH|MNT_EXPIRE|UMOUNT_NOFOLLOW|UMOUNT_UNUSED)[[:space:]]+/ {
            print
        }
    ' "$source_file" > "$tmp_defs"

    if [ ! -s "$tmp_defs" ]; then
        echo "failed to derive linux/umount.h ABI supplement from: $source_file" >&2
        exit 1
    fi

    {
        echo "/* SPDX-License-Identifier: GPL-2.0 */"
        echo "#ifndef _LINUX_UMOUNT_H"
        echo "#define _LINUX_UMOUNT_H"
        echo
        cat "$tmp_defs"
        echo
        echo "#endif"
    } > "$out_file"
}

write_statfs_abi_header() {
    local source_file=$1
    local out_file=$2
    local tmp_defs="$tmp/statfs-defines.h"

    awk '
        /^#define[[:space:]]+ST_(RDONLY|NOSUID|NODEV|NOEXEC|SYNCHRONOUS|VALID|MANDLOCK|NOATIME|NODIRATIME|RELATIME|NOSYMFOLLOW)[[:space:]]+/ {
            print
        }
    ' "$source_file" > "$tmp_defs"

    if [ ! -s "$tmp_defs" ]; then
        echo "failed to derive linux/statfs.h ABI supplement from: $source_file" >&2
        exit 1
    fi

    {
        echo "/* SPDX-License-Identifier: GPL-2.0 */"
        echo "#ifndef _LINUX_STATFS_H"
        echo "#define _LINUX_STATFS_H"
        echo
        cat "$tmp_defs"
        echo
        echo "#endif"
    } > "$out_file"
}

write_include_paths() {
    local kheaders_root_rel="${vendor_root}/kheaders/${linux_version}/${linux_arch}"
    local mk_file="$1/include-paths.mk"
    local xcconfig_file="$1/include-paths.xcconfig"

    cat > "$mk_file" <<EOF
LINUX_KHEADERS_ROOT := ${kheaders_root_rel}
LINUX_KHEADERS_CPPFLAGS := \\
  -I\$(LINUX_KHEADERS_ROOT)/objtree/arch/${linux_arch}/include/generated \\
  -I\$(LINUX_KHEADERS_ROOT)/srctree/arch/${linux_arch}/include \\
  -I\$(LINUX_KHEADERS_ROOT)/objtree/include \\
  -I\$(LINUX_KHEADERS_ROOT)/srctree/include
EOF

    cat > "$xcconfig_file" <<EOF
LINUX_KHEADERS_ROOT = \$(SRCROOT)/${vendor_root}/kheaders/${linux_version}/${linux_arch}
LINUX_KHEADERS_SEARCH_PATHS = \$(LINUX_KHEADERS_ROOT)/objtree/arch/${linux_arch}/include/generated \$(LINUX_KHEADERS_ROOT)/srctree/arch/${linux_arch}/include \$(LINUX_KHEADERS_ROOT)/objtree/include \$(LINUX_KHEADERS_ROOT)/srctree/include
EOF
}

write_provenance() {
    local kheaders_root=$1
    local tarball_sha=$2
    local generated_at

    generated_at=$(LC_ALL=C date -u +"%Y-%m-%dT%H:%M:%SZ")

    cat > "$kheaders_root/provenance.json" <<EOF
{
  "linux_version": "${linux_version}",
  "linux_arch": "${linux_arch}",
  "kernel_series": "${kernel_series}",
  "tarball_url": "${tarball_url}",
  "source_release_name": "linux-${linux_version}",
  "generated_at_utc": "${generated_at}",
  "generator_script": "scripts/vendor_linux_headers.sh",
  "make_targets_run": [
    "defconfig",
    "olddefconfig",
    "prepare",
    "modules_prepare",
    "headers_install"
  ],
  "uapi_root": "${vendor_root}/uapi/${linux_version}/${linux_arch}",
  "kheaders_root": "${vendor_root}/kheaders/${linux_version}/${linux_arch}",
  "abi_root": "${vendor_root}/abi/${linux_version}/${linux_arch}",
  "downloaded_tarball_sha256": "${tarball_sha}",
  "git_commit": null
}
EOF
}

write_manifest() {
    local stage_vendor_root=$1
    local manifest_file=$2
    local tmp_list="$tmp/manifest-files.txt"
    local rel_path
    local abs_path

    : > "$tmp_list"
    find "$stage_vendor_root/uapi/${linux_version}/${linux_arch}" -type f -print >> "$tmp_list"
    find "$stage_vendor_root/kheaders/${linux_version}/${linux_arch}" -type f ! -name manifest.sha256 -print >> "$tmp_list"
    find "$stage_vendor_root/abi/${linux_version}/${linux_arch}" -type f -print >> "$tmp_list"

    LC_ALL=C sort "$tmp_list" | while IFS= read -r abs_path; do
        rel_path=${abs_path#"$stage_vendor_root"/}
        printf '%s  %s\n' "$(shasum -a 256 "$abs_path" | awk '{print $1}')" "${vendor_root}/${rel_path}"
    done > "$manifest_file"
}

validate_vendor_tree() {
    local stage_vendor_root=$1
    local uapi_root="$stage_vendor_root/uapi/${linux_version}/${linux_arch}/include"
    local kheaders_root="$stage_vendor_root/kheaders/${linux_version}/${linux_arch}"
    local abi_root="$stage_vendor_root/abi/${linux_version}/${linux_arch}/include"
    local generated_asm_dir="$kheaders_root/objtree/arch/${linux_arch}/include/generated/asm"

    require_file "$uapi_root/linux/wait.h"
    require_file "$uapi_root/asm/signal.h"
    require_file "$uapi_root/asm-generic/errno-base.h"
    require_file "$uapi_root/linux/futex.h"
    require_file "$uapi_root/linux/seccomp.h"
    if ! find "$uapi_root" -type f | grep -q .; then
        echo "empty UAPI root: $uapi_root" >&2
        exit 1
    fi

    require_file "$kheaders_root/srctree/include/linux/fs.h"
    require_file "$kheaders_root/srctree/include/linux/sched.h"
    require_file "$kheaders_root/srctree/arch/${linux_arch}/include/asm/unistd.h"
    require_file "$kheaders_root/objtree/include/generated/autoconf.h"
    require_file "$kheaders_root/objtree/include/generated/utsrelease.h"
    if ! find "$kheaders_root/srctree/include" -type f | grep -q .; then
        echo "empty kernel srctree include root: $kheaders_root/srctree/include" >&2
        exit 1
    fi
    require_dir "$generated_asm_dir"
    if ! find "$generated_asm_dir" -type f | grep -q .; then
        echo "missing generated arch asm headers under: $generated_asm_dir" >&2
        exit 1
    fi

    require_file "$abi_root/linux/statfs.h"
    require_file "$abi_root/linux/umount.h"
    if ! find "$abi_root" -type f | grep -q .; then
        echo "empty ABI root: $abi_root" >&2
        exit 1
    fi
}

tarball="$tmp/linux-${linux_version}.tar.xz"
src="$tmp/linux-${linux_version}"
obj="$tmp/obj-${linux_version}-${linux_arch}"
uapi_out="$tmp/uapi-out"
stage_vendor_root="$tmp/vendor-root"
host_compat_include="$tmp/host-compat/include"
host_tool_bin="$tmp/host-tools/bin"
linux_make_env=(LLVM=1 LLVM_IAS=1 HOSTCC=clang CC=clang LD=ld.lld)

mkdir -p "$host_compat_include"
mkdir -p "$host_tool_bin"

mkdir -p "$uapi_out" "$stage_vendor_root"

curl -fL --retry 5 --retry-delay 1 --retry-all-errors "$tarball_url" -o "$tarball"
tarball_sha=$(shasum -a 256 "$tarball" | awk '{print $1}')

tar -xf "$tarball" -C "$tmp"
require_dir "$src"

cp "$repo_root/scripts/linux_host_compat/elf.h" "$host_compat_include/elf.h"
cp "$repo_root/scripts/linux_host_compat/endian.h" "$host_compat_include/endian.h"
cp "$repo_root/scripts/linux_host_compat/byteswap.h" "$host_compat_include/byteswap.h"
cp "$repo_root/scripts/linux_host_compat/linux_arm_elf_compat.h" "$host_compat_include/linux_arm_elf_compat.h"

perl -0pi -e 's/#include "modpost.h"/#define _UUID_T\n#define uuid_t int\n#include "modpost.h"\n#undef uuid_t/' "$src/scripts/mod/file2alias.c"

linux_make_env+=("HOSTCFLAGS=-I$host_compat_include -include $host_compat_include/linux_arm_elf_compat.h")
if [ -n "$linux_sed" ]; then
    ln -sf "$linux_sed" "$host_tool_bin/sed"
    export PATH="$host_tool_bin:$PATH"
fi

"$linux_make" -C "$src" O="$obj" ARCH="$linux_arch" "${linux_make_env[@]}" defconfig
if [ -x "$src/scripts/config" ]; then
    "$src/scripts/config" --file "$obj/.config" -d BUILDTIME_TABLE_SORT || true
fi
"$linux_make" -C "$src" O="$obj" ARCH="$linux_arch" "${linux_make_env[@]}" olddefconfig
"$linux_make" -C "$src" O="$obj" ARCH="$linux_arch" "${linux_make_env[@]}" prepare
"$linux_make" -C "$src" O="$obj" ARCH="$linux_arch" "${linux_make_env[@]}" modules_prepare
"$linux_make" -C "$src" O="$obj" ARCH="$linux_arch" "${linux_make_env[@]}" headers_install INSTALL_HDR_PATH="$uapi_out"

require_dir "$uapi_out/include"
require_dir "$src/include"
require_dir "$src/arch/$linux_arch/include"
require_dir "$obj/include/generated"
require_dir "$obj/include/config"
require_dir "$obj/arch/$linux_arch/include/generated"

uapi_dest="$stage_vendor_root/uapi/${linux_version}/${linux_arch}/include"
kheaders_root="$stage_vendor_root/kheaders/${linux_version}/${linux_arch}"
abi_dest="$stage_vendor_root/abi/${linux_version}/${linux_arch}/include/linux"

copy_tree "$uapi_out/include" "$uapi_dest"
if [ -d "$uapi_dest/include" ] && [ ! -d "$uapi_dest/linux" ]; then
    normalized_uapi="$tmp/uapi-normalized"
    mkdir -p "$normalized_uapi"
    cp -R "$uapi_dest/include/." "$normalized_uapi"
    rm -rf "$uapi_dest"
    mkdir -p "$uapi_dest"
    cp -R "$normalized_uapi/." "$uapi_dest"
fi
copy_tree "$src/include" "$kheaders_root/srctree/include"
copy_tree "$src/arch/$linux_arch/include" "$kheaders_root/srctree/arch/$linux_arch/include"
copy_tree "$obj/include/generated" "$kheaders_root/objtree/include/generated"
copy_tree "$obj/include/config" "$kheaders_root/objtree/include/config"
copy_tree "$obj/arch/$linux_arch/include/generated" "$kheaders_root/objtree/arch/$linux_arch/include/generated"

mkdir -p "$abi_dest"
write_statfs_abi_header "$src/include/linux/statfs.h" "$abi_dest/statfs.h"
write_umount_abi_header "$src/include/linux/fs.h" "$abi_dest/umount.h"

write_include_paths "$kheaders_root"
write_provenance "$kheaders_root" "$tarball_sha"
write_manifest "$stage_vendor_root" "$kheaders_root/manifest.sha256"
validate_vendor_tree "$stage_vendor_root"

mkdir -p "$stage_vendor_root"
cp "$repo_root/third_party/linux/README.md" "$stage_vendor_root/README.md" 2>/dev/null || true

mkdir -p "$(dirname "$final_vendor_root")"
rm -rf "$final_vendor_root"
mv "$stage_vendor_root" "$final_vendor_root"

echo "vendored roots:"
echo "  $final_vendor_root/uapi/${linux_version}/${linux_arch}"
echo "  $final_vendor_root/kheaders/${linux_version}/${linux_arch}"
echo "  $final_vendor_root/abi/${linux_version}/${linux_arch}"
