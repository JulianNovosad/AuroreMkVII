#!/bin/bash
# build-native.sh - Native build on this host (aarch64 Pi or x86_64 dev machine)
# Usage: ./scripts/build-native.sh [Debug|Release|RelWithDebInfo]
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_TYPE="${1:-Release}"
BUILD_DIR="$PROJECT_DIR/build-native"

echo "=== Aurore MkVII Native Build ==="
echo "Build type:      $BUILD_TYPE"
echo "Build directory: $BUILD_DIR"
echo "Processor:       $(uname -m)"
echo ""

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake .. \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DAURORE_ENABLE_TESTS=ON \
    -DAURORE_REALTIME=ON

cmake --build . -j"$(nproc)"

echo ""
echo "=== Build complete ==="
