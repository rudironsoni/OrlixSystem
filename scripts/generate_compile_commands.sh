#!/bin/bash
set -e

cd "$(dirname "$0")/.."

echo "=== Generating compile_commands.json for IXLandKernel ==="

# This script generates compile_commands.json from Xcode build output.
# The generated file contains absolute paths specific to your machine.
# Regenerate after pulling changes or switching branches.

rm -rf .clangd-cache/cdb
mkdir -p .clangd-cache/cdb

# Generate Xcode project
xcodegen generate --project .

# Build and generate compilation database fragments
xcodebuild \
  -project IXLandKernel.xcodeproj \
  -scheme IXLandKernel-6.12-arm64 \
  -sdk iphonesimulator \
  -arch arm64 \
  -configuration Debug \
  OTHER_CFLAGS="$(inherited) -gen-cdb-fragment-path $(pwd)/.clangd-cache/cdb" \
  build

echo "=== Combining fragments into compile_commands.json ==="

cd .clangd-cache/cdb

first=true
echo "[" > ../compile_commands.json

for f in *.json; do
    if [ -f "$f" ]; then
        if [ "$first" = true ]; then
            first=false
        else
            echo "," >> ../compile_commands.json
        fi
        cat "$f" >> ../compile_commands.json
    fi
done

echo "]" >> ../compile_commands.json

mv ../compile_commands.json ../../compile_commands.json

echo "=== Done: compile_commands.json created at repo root ==="
echo "Entries: $(grep -c '"command"' ../../compile_commands.json || echo 0)"
echo ""
echo "NOTE: compile_commands.json contains absolute paths and is machine-specific."
echo "It should NOT be committed to git. Run this script after cloning or pulling changes."
