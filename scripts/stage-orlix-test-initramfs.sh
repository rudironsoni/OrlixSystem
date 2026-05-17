#!/usr/bin/env bash
set -euo pipefail

profile="${PROFILE:-appstore}"
linux_version="${LINUX_VERSION:-6.12}"
linux_arch="${LINUX_ARCH:-orlix}"
kselftest_install=""
output=""

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
        --kselftest-install)
            kselftest_install="$2"
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

if [ -z "$kselftest_install" ] || [ -z "$output" ]; then
    printf 'usage: %s --kselftest-install DIR --output OrlixTestInitramfs.bundle [--profile appstore]\n' "$0" >&2
    exit 1
fi

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

case "$output" in
    "$repo_root"/Build/OrlixKernel/test-initramfs/OrlixTestInitramfs.bundle|Build/OrlixKernel/test-initramfs/OrlixTestInitramfs.bundle) ;;
    *)
        printf 'refusing to write test initramfs resource outside Build/OrlixKernel/test-initramfs: %s\n' "$output" >&2
        exit 1
        ;;
esac

if [ -L Build ] || [ -L Build/OrlixKernel ] || [ -L Build/OrlixKernel/test-initramfs ] || [ -L "$output" ]; then
    printf 'refusing to stage test initramfs through symlinked Build path\n' >&2
    exit 1
fi

if [ ! -d "$kselftest_install" ]; then
    printf 'missing upstream kselftest install directory: %s\n' "$kselftest_install" >&2
    exit 1
fi

for required in \
    "$kselftest_install/run_kselftest.sh" \
    "$kselftest_install/kselftest/runner.sh" \
    "$kselftest_install/kselftest-list.txt" \
    "$kselftest_install/orlix/milestone2_boot_contract" \
    "$kselftest_install/orlix/milestone3_boot_probe_contract"; do
    if [ ! -s "$required" ]; then
        printf 'missing non-empty kselftest install input: %s\n' "$required" >&2
        exit 1
    fi
done

if ! grep -qx 'orlix:milestone2_boot_contract' "$kselftest_install/kselftest-list.txt"; then
    printf 'kselftest install list is missing orlix:milestone2_boot_contract\n' >&2
    exit 1
fi
if ! grep -qx 'orlix:milestone3_boot_probe_contract' "$kselftest_install/kselftest-list.txt"; then
    printf 'kselftest install list is missing orlix:milestone3_boot_probe_contract\n' >&2
    exit 1
fi

output_parent="$(dirname "$output")"
rm -rf "$output"
mkdir -p "$output_parent" "$output/kselftest"
cp -R "$kselftest_install/." "$output/kselftest"

cat > "$output/init" <<'EOF'
#!/bin/sh
set -eu

mkdir -p /proc /sys /sys/kernel/debug /dev
mount -t proc proc /proc 2>/dev/null || true
mount -t sysfs sysfs /sys 2>/dev/null || true
mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null || true

echo "ORLIX-KUNIT-BEGIN"
if [ -d /sys/kernel/debug/kunit ]; then
    found_kunit_results=0
    for result in /sys/kernel/debug/kunit/*/results; do
        if [ -r "$result" ]; then
            found_kunit_results=1
            cat "$result"
        fi
    done
    if [ "$found_kunit_results" -eq 0 ]; then
        dmesg || true
    fi
else
    dmesg || true
fi
echo "ORLIX-KUNIT-END"

echo "ORLIX-KSELFTEST-BEGIN"
cd /kselftest
set +e
./run_kselftest.sh -c orlix
kselftest_status="$?"
set -e
echo "ORLIX-KSELFTEST-END status=$kselftest_status"
exit "$kselftest_status"
EOF
chmod 755 "$output/init"

printf '%s\n' "$profile" > "$output/selected_profile.txt"
printf '%s\n' "$linux_arch" > "$output/linux_arch.txt"
printf '%s\n' "$linux_version" > "$output/linux_version.txt"
shasum -a 256 "$output/kselftest/kselftest-list.txt" | awk '{ print $1 }' > "$output/kselftest-list.sha256"

cat > "$output/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleIdentifier</key>
    <string>org.orlix.OrlixTestInitramfs</string>
    <key>CFBundleName</key>
    <string>OrlixTestInitramfs</string>
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
</dict>
</plist>
EOF

plutil -lint "$output/Info.plist" >/dev/null
printf 'staged Orlix test initramfs resource: %s (profile %s)\n' "$output" "$profile"
