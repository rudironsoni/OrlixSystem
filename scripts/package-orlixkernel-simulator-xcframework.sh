#!/usr/bin/env bash
set -euo pipefail

framework=""
output=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        --framework)
            framework="$2"
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

if [ -z "$framework" ] || [ -z "$output" ]; then
    printf 'usage: %s --framework OrlixKernel.framework --output OrlixKernel.xcframework\n' "$0" >&2
    exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "$script_dir/.." && pwd -P)"
cd "$repo_root"

case "$output" in
    "$repo_root"/Build/OrlixKernel/xcframework/OrlixKernel.xcframework|Build/OrlixKernel/xcframework/OrlixKernel.xcframework) ;;
    *)
        printf 'refusing to write simulator XCFramework outside Build/OrlixKernel/xcframework: %s\n' "$output" >&2
        exit 1
        ;;
esac

if [ ! -d "$framework" ]; then
    printf 'missing simulator framework: %s\n' "$framework" >&2
    exit 1
fi

if [ -L Build ] || [ -L Build/OrlixKernel ] || [ -L Build/OrlixKernel/xcframework ] || [ -L "$output" ]; then
    printf 'refusing to package simulator XCFramework through symlinked Build path\n' >&2
    exit 1
fi

for required in \
    "$framework/OrlixKernel" \
    "$framework/Info.plist"; do
    if [ ! -s "$required" ]; then
        printf 'missing non-empty framework input: %s\n' "$required" >&2
        exit 1
    fi
done

output_parent="$(dirname "$output")"
rm -rf "$output"
mkdir -p "$output_parent" "$output/ios-arm64-simulator"
cp -R "$framework" "$output/ios-arm64-simulator/OrlixKernel.framework"

cat > "$output/Info.plist" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>AvailableLibraries</key>
    <array>
        <dict>
            <key>LibraryIdentifier</key>
            <string>ios-arm64-simulator</string>
            <key>LibraryPath</key>
            <string>OrlixKernel.framework</string>
            <key>SupportedArchitectures</key>
            <array>
                <string>arm64</string>
            </array>
            <key>SupportedPlatform</key>
            <string>ios</string>
            <key>SupportedPlatformVariant</key>
            <string>simulator</string>
        </dict>
    </array>
    <key>CFBundlePackageType</key>
    <string>XFWK</string>
    <key>XCFrameworkFormatVersion</key>
    <string>1.0</string>
</dict>
</plist>
EOF

plutil -lint "$output/Info.plist" >/dev/null
printf 'packaged simulator OrlixKernel XCFramework: %s\n' "$output"
