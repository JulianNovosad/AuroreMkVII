#!/bin/bash
# build-native.sh - Build Aurore MkVII for native architecture (laptop/desktop)
#
# Usage: ./scripts/build-native.sh [Debug|Release|RelWithDebInfo]
#
# This script builds the project for your current system architecture.
# Use this for fast iteration during development and running unit tests.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_TYPE="${1:-Release}"
BUILD_DIR="$PROJECT_DIR/build-native"

echo "=== Aurore MkVII Native Build ==="
echo "Build type: $BUILD_TYPE"
echo "Build directory: $BUILD_DIR"
echo ""

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure
echo "Configuring..."
cmake .. \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DAURORE_ENABLE_TESTS=ON \
    -DAURORE_REALTIME=ON

# Build
echo ""
echo "Building..."
cmake --build . -j"$(nproc)"

# Summary
echo ""
echo "=== Build Complete ==="
echo "Executables:"
ls -lh aurore 2>/dev/null || echo "  (main executable not built)"
ls -lh *_test 2>/dev/null || echo "  (test executables not built)"
echo ""
echo "To run tests:"
echo "  cd $BUILD_DIR && ctest --output-on-failure"
echo ""
echo "To run the application:"
echo "  cd $BUILD_DIR && ./aurore --help"
echo ""
