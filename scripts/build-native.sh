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

# Protobuf hint: if protoc is not in the system PATH, look in ~/.local/bin
# (installed without sudo via: apt-get download protobuf-compiler && dpkg-deb -x ...)
PROTOBUF_ARGS=()
LOCAL_PROTOC="$HOME/.local/bin/protoc"
LOCAL_PROTOBUF_LIB="$HOME/.local/lib/x86_64-linux-gnu/libprotobuf.so"
LOCAL_PROTOBUF_INC="$HOME/.local/include"
if [ -x "$LOCAL_PROTOC" ] && [ ! -x "$(command -v protoc 2>/dev/null)" ]; then
    PROTOBUF_ARGS+=(
        "-DProtobuf_INCLUDE_DIR=$LOCAL_PROTOBUF_INC"
        "-DProtobuf_LIBRARY=$LOCAL_PROTOBUF_LIB"
        "-DProtobuf_PROTOC_EXECUTABLE=$LOCAL_PROTOC"
    )
    export PATH="$HOME/.local/bin:$PATH"
    export LD_LIBRARY_PATH="$HOME/.local/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}"
fi

cmake .. \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DAURORE_ENABLE_TESTS=ON \
    -DAURORE_REALTIME=ON \
    "${PROTOBUF_ARGS[@]}" \
    "${@:2}"

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
