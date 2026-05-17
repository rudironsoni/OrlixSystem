#!/usr/bin/env bash
set -euo pipefail

profile="${PROFILE:-appstore}"
linux_version="${LINUX_VERSION:-6.12}"
linux_arch="${LINUX_ARCH:-orlix}"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --profile)
            profile="$2"
            shift 2
            ;;
        --linux-version)
            linux_version="$2"
            shift 2
            ;;
        --linux-arch)
            linux_arch="$2"
            shift 2
            ;;
        *)
            printf 'unknown argument: %s\n' "$1" >&2
            exit 1
            ;;
    esac
done

case "$profile" in
    appstore|development) ;;
    *)
        printf 'unsupported PROFILE=%s (expected one of: appstore development)\n' "$profile" >&2
        exit 1
        ;;
esac

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "$script_dir/.." && pwd -P)"
cd "$repo_root"

vmlinux="Build/OrlixKernel/build/$profile/vmlinux"
dtb_dir="Build/OrlixKernel/build/$profile/arch/$linux_arch/boot/dts"
payload_parent="Build/OrlixKernel/vmlinux-tooling"
payload_dir="$payload_parent/OrlixKernelPayload.bundle"

if [ -L Build ] || [ -L Build/OrlixKernel ] || [ -L "$payload_parent" ] || [ -L "$payload_dir" ]; then
    printf 'refusing to stage payload through symlinked Build path\n' >&2
    exit 1
fi

if [ ! -f "$vmlinux" ]; then
    printf 'missing optional vmlinux tooling artifact: %s\n' "$vmlinux" >&2
    exit 1
fi

for dtb in appstore development; do
    if [ ! -f "$dtb_dir/$dtb.dtb" ]; then
        printf 'missing profile DTB: %s\n' "$dtb_dir/$dtb.dtb" >&2
        exit 1
    fi
done

hash_line="$(shasum -a 256 "$vmlinux")"
vmlinux_hash="${hash_line%% *}"

rm -rf "$payload_dir"
mkdir -p "$payload_dir/dtbs"

cp "$vmlinux" "$payload_dir/vmlinux"
for dtb in appstore development; do
    cp "$dtb_dir/$dtb.dtb" "$payload_dir/dtbs/$dtb.dtb"
done

printf '%s\n' "$profile" > "$payload_dir/selected_profile.txt"
printf '%s\n' "$linux_arch" > "$payload_dir/linux_arch.txt"
printf '%s\n' "$linux_version" > "$payload_dir/linux_version.txt"
printf '%s\n' "$vmlinux_hash" > "$payload_dir/vmlinux.sha256"

cat > "$payload_dir/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleIdentifier</key>
    <string>org.orlix.OrlixKernelPayload</string>
    <key>CFBundleName</key>
    <string>OrlixKernelPayload</string>
    <key>CFBundlePackageType</key>
    <string>BNDL</string>
    <key>CFBundleShortVersionString</key>
    <string>0.1</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>OrlixLinuxArch</key>
    <string>$linux_arch</string>
    <key>OrlixLinuxVersion</key>
    <string>$linux_version</string>
    <key>OrlixProfile</key>
    <string>$profile</string>
    <key>OrlixVmlinuxSHA256</key>
    <string>$vmlinux_hash</string>
</dict>
</plist>
EOF

printf 'staged optional OrlixKernel vmlinux tooling payload: %s (profile %s, vmlinux sha256 %s)\n' "$payload_dir" "$profile" "$vmlinux_hash"
