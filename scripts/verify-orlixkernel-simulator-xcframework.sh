#!/usr/bin/env bash
set -euo pipefail

xcframework=""
profile="${PROFILE:-appstore}"
linux_version="${LINUX_VERSION:-6.12}"
linux_arch="${LINUX_ARCH:-orlix}"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --xcframework)
            xcframework="$2"
            shift 2
            ;;
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

if [ -z "$xcframework" ]; then
    printf 'usage: %s --xcframework OrlixKernel.xcframework [--profile appstore]\n' "$0" >&2
    exit 1
fi

case "$profile" in
    appstore|development) ;;
    *)
        printf 'unsupported PROFILE=%s (expected one of: appstore development)\n' "$profile" >&2
        exit 1
        ;;
esac

framework="$xcframework/ios-arm64-simulator/OrlixKernel.framework"

for required in \
    "$xcframework/Info.plist" \
    "$framework/Info.plist" \
    "$framework/OrlixKernel"; do
    if [ ! -s "$required" ]; then
        printf 'missing non-empty XCFramework input: %s\n' "$required" >&2
        exit 1
    fi
done

plutil -lint "$xcframework/Info.plist" "$framework/Info.plist" >/dev/null

printf 'verified simulator OrlixKernel XCFramework wrapper: %s (profile %s)\n' "$xcframework" "$profile"
