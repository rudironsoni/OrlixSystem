#!/usr/bin/env bash
set -euo pipefail

mirror="${ORLIX_DEBIAN_MIRROR:-https://deb.debian.org/debian}"
suite="${ORLIX_DEBIAN_SUITE:-bookworm}"
debian_arch="${ORLIX_DEBIAN_ARCH:-arm64}"
gcc_version="${ORLIX_DEBIAN_GCC_VERSION:-12}"
output=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        --mirror)
            mirror="$2"
            shift 2
            ;;
        --suite)
            suite="$2"
            shift 2
            ;;
        --debian-arch)
            debian_arch="$2"
            shift 2
            ;;
        --gcc-version)
            gcc_version="$2"
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

if [ -z "$output" ]; then
    printf 'usage: %s --output Build/OrlixKernel/linux-userspace-sysroot/aarch64\n' "$0" >&2
    exit 1
fi

case "$debian_arch" in
    arm64) ;;
    *)
        printf 'unsupported Debian architecture: %s\n' "$debian_arch" >&2
        exit 1
        ;;
esac

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "$script_dir/.." && pwd -P)"
cd "$repo_root"

case "$output" in
    "$repo_root"/Build/OrlixKernel/linux-userspace-sysroot/aarch64|Build/OrlixKernel/linux-userspace-sysroot/aarch64) ;;
    *)
        printf 'refusing to write Linux userspace sysroot outside Build/OrlixKernel/linux-userspace-sysroot/aarch64: %s\n' "$output" >&2
        exit 1
        ;;
esac

if [ -L Build ] || [ -L Build/OrlixKernel ] || [ -L Build/OrlixKernel/linux-userspace-sysroot ] || [ -L "$output" ]; then
    printf 'refusing to bootstrap Linux userspace sysroot through symlinked Build path\n' >&2
    exit 1
fi

for tool in awk ar curl gzip shasum tar; do
    command -v "$tool" >/dev/null 2>&1 || { printf '%s is required to bootstrap the Linux userspace sysroot\n' "$tool" >&2; exit 1; }
done

output_parent="$(dirname "$output")"
mkdir -p "$output_parent"
work_dir="$(mktemp -d "$output_parent/.bootstrap.XXXXXX")"
case "$work_dir" in
    /*) work_dir_abs="$work_dir" ;;
    *) work_dir_abs="$repo_root/$work_dir" ;;
esac
trap 'rm -rf "$work_dir"' EXIT
mkdir -p "$work_dir_abs/debs" "$work_dir_abs/root"

packages_index="$work_dir_abs/Packages"
curl -fsSL "$mirror/dists/$suite/main/binary-$debian_arch/Packages.gz" -o "$work_dir_abs/Packages.gz"
gzip -dc "$work_dir_abs/Packages.gz" > "$packages_index"

package_filename()
{
    package="$1"
    awk -v package="$package" '
        BEGIN { RS=""; FS="\n" }
        $0 ~ "Package: " package "\n" {
            for (i = 1; i <= NF; i++) {
                if ($i ~ /^Filename: /) {
                    sub(/^Filename: /, "", $i)
                    print $i
                    exit
                }
            }
        }
    ' "$packages_index"
}

extract_deb()
{
    package="$1"
    filename="$(package_filename "$package")"
    if [ -z "$filename" ]; then
        printf 'Debian package not found in %s/%s for %s: %s\n' "$suite" "$debian_arch" "$mirror" "$package" >&2
        exit 1
    fi

    deb="$work_dir_abs/debs/$package.deb"
    curl -fsSL "$mirror/$filename" -o "$deb"

    extract_dir="$work_dir_abs/extract-$package"
    mkdir -p "$extract_dir"
    (
        cd "$extract_dir"
        ar -x "$deb"
        data_archive="$(printf '%s\n' data.tar.*)"
        tar -xf "$data_archive" -C "$work_dir_abs/root"
    )
}

extract_deb libc6
extract_deb libc6-dev
extract_deb linux-libc-dev
extract_deb gcc-$gcc_version-base
extract_deb libgcc-s1
extract_deb libgcc-$gcc_version-dev

for required in \
    "$work_dir_abs/root/usr/include/stdio.h" \
    "$work_dir_abs/root/usr/lib/aarch64-linux-gnu/libc.a" \
    "$work_dir_abs/root/usr/lib/gcc/aarch64-linux-gnu/$gcc_version/crtbeginT.o" \
    "$work_dir_abs/root/usr/lib/gcc/aarch64-linux-gnu/$gcc_version/libgcc.a"; do
    if [ ! -s "$required" ]; then
        printf 'missing non-empty sysroot input after Debian extraction: %s\n' "$required" >&2
        exit 1
    fi
done

rm -rf "$output"
mv "$work_dir_abs/root" "$output"

cat > "$output/orlix-sysroot-source.txt" <<EOF
mirror=$mirror
suite=$suite
debian_arch=$debian_arch
gcc_version=$gcc_version
packages=libc6 libc6-dev linux-libc-dev gcc-$gcc_version-base libgcc-s1 libgcc-$gcc_version-dev
EOF

shasum -a 256 "$packages_index" | awk '{ print $1 }' > "$output/orlix-packages-index.sha256"
printf 'bootstrapped Orlix Linux userspace sysroot: %s (%s %s)\n' "$output" "$suite" "$debian_arch"
