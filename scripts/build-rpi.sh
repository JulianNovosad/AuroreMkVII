#!/bin/bash
# build-rpi.sh - Cross-compile Aurore MkVII for Raspberry Pi 5 (aarch64)
#
# Usage: ./scripts/build-rpi.sh [Debug|Release|RelWithDebInfo]
#
# Prerequisites:
#   sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
#
# IMPORTANT: Full cross-compilation requires ARM64 versions of dependencies
# (OpenCV, libcamera) which aren't typically available on x86_64 systems.
#
# Recommended approaches:
#   1. Build natively on Raspberry Pi 5 (recommended - see below)
#   2. Set up a sysroot with ARM64 libraries (advanced)
#   3. Use this script to verify toolchain configuration
#
# Native build on RPi 5 (SSH from laptop):
#   ssh pi@aurorpi.local
#   cd ~/AuroreMkVII
#   mkdir build && cd build
#   cmake .. -DCMAKE_BUILD_TYPE=Release
#   cmake --build . -j4

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_TYPE="${1:-Release}"
BUILD_DIR="$PROJECT_DIR/build-rpi"
TOOLCHAIN_FILE="$PROJECT_DIR/cmake/aarch64-rpi5-toolchain.cmake"

echo "=== Aurore MkVII Raspberry Pi 5 Cross-Compile ==="
echo "Build type: $BUILD_TYPE"
echo "Build directory: $BUILD_DIR"
echo "Toolchain: $TOOLCHAIN_FILE"
echo ""

# Check for cross-compiler
if ! command -v aarch64-linux-gnu-g++ &> /dev/null; then
    echo "ERROR: Cross-compiler not found!"
    echo ""
    echo "Install with:"
    echo "  sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu"
    echo ""
    exit 1
fi

# Check for toolchain file
if [[ ! -f "$TOOLCHAIN_FILE" ]]; then
    echo "ERROR: Toolchain file not found: $TOOLCHAIN_FILE"
    exit 1
fi

echo "NOTE: Cross-compilation requires ARM64 versions of dependencies."
echo "      If the build fails, consider building natively on your RPi 5:"
echo ""
echo "      ssh pi@hostname"
echo "      cd ~/AuroreMkVII && mkdir build && cd build"
echo "      cmake .. -DCMAKE_BUILD_TYPE=Release"
echo "      cmake --build . -j4"
echo ""
read -p "Continue with cross-compile attempt? (y/N): " -n 1 -r
echo ""
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Aborted."
    exit 0
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure
echo "Configuring..."
if ! cmake .. \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DAURORE_ENABLE_TESTS=ON \
    -DAURORE_REALTIME=ON; then
    echo ""
    echo "ERROR: Configuration failed!"
    echo ""
    echo "This is expected if ARM64 dependency libraries aren't installed."
    echo "Recommendation: Build natively on your Raspberry Pi 5 instead."
    exit 1
fi

# Build
echo ""
echo "Building..."
cmake --build . -j"$(nproc)"

# Verify architecture
echo ""
echo "=== Verifying Binary Architecture ==="
if [[ -f "aurore" ]]; then
    file aurore
    echo ""
fi

# Summary
echo "=== Cross-Compile Complete ==="
echo "Executables:"
ls -lh aurore 2>/dev/null || echo "  (main executable not built)"
ls -lh *_test 2>/dev/null || echo "  (test executables not built)"
echo ""
echo "To deploy to Raspberry Pi 5:"
echo "  ./scripts/deploy-to-rpi.sh [pi@hostname]"
echo ""
echo "Note: Test executables are for aarch64 and cannot run on this x86_64 system."
echo "      Deploy to RPi 5 to run tests."
echo ""
