#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build-release"

echo "=== Aurore MkVII Release Build ==="
echo "Build directory: $BUILD_DIR"
echo ""

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DAURORE_ENABLE_TESTS=ON \
    -DAURORE_REALTIME=ON

cmake --build . -j"$(nproc)"

echo ""
echo "=== Build complete: $BUILD_DIR ==="
