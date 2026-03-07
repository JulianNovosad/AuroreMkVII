#!/bin/bash
# scripts/setup-production.sh
# Production deployment setup for AuroreMkVII
#
# This script installs all required system packages and dependencies
# for secure production deployment.
#
# Usage: sudo ./scripts/setup-production.sh

set -e

echo "=== AuroreMkVII Production Setup ==="
echo ""

# Update package lists
echo "[1/6] Updating package lists..."
apt update

# Install libcap-dev for privilege drop (SEC-001)
echo "[2/6] Installing libcap-dev for privilege drop..."
apt install -y libcap-dev

# Update system packages (fixes CVE-2024-2961 in glibc)
echo "[3/6] Updating system packages (CVE patches)..."
apt upgrade -y

# Check libwebp version (CVE-2023-4863)
echo "[4/6] Checking libwebp version (CVE-2023-4863)..."
WEBP_VERSION=$(pkg-config --modversion libwebp 2>/dev/null || echo "not installed")
echo "  libwebp version: $WEBP_VERSION"
if [ "$WEBP_VERSION" != "not installed" ]; then
    WEBP_MAJOR=$(echo "$WEBP_VERSION" | cut -d. -f1)
    WEBP_MINOR=$(echo "$WEBP_VERSION" | cut -d. -f2)
    WEBP_PATCH=$(echo "$WEBP_VERSION" | cut -d. -f3)
    
    if [ "$WEBP_MAJOR" -lt 1 ] || \
       ([ "$WEBP_MAJOR" -eq 1 ] && [ "$WEBP_MINOR" -lt 3 ]) || \
       ([ "$WEBP_MAJOR" -eq 1 ] && [ "$WEBP_MINOR" -eq 3 ] && [ "$WEBP_PATCH" -lt 2 ]); then
        echo "  WARNING: libwebp < 1.3.2 is vulnerable to CVE-2023-4863"
        echo "  Installing updated libwebp..."
        apt install -y libwebp-dev libwebp7
    else
        echo "  libwebp version is secure (>= 1.3.2)"
    fi
else
    echo "  Installing libwebp..."
    apt install -y libwebp-dev libwebp7
fi

# Install OpenCV 4.9.0+ (CVE-2024-2167)
echo "[5/6] Checking OpenCV version (CVE-2024-2167)..."
OPENCV_VERSION=$(pkg-config --modversion opencv4 2>/dev/null || echo "not installed")
echo "  OpenCV version: $OPENCV_VERSION"

if [ "$OPENCV_VERSION" != "not installed" ]; then
    OPENCV_MAJOR=$(echo "$OPENCV_VERSION" | cut -d. -f1)
    OPENCV_MINOR=$(echo "$OPENCV_VERSION" | cut -d. -f2)
    
    if [ "$OPENCV_MAJOR" -lt 4 ] || ([ "$OPENCV_MAJOR" -eq 4 ] && [ "$OPENCV_MINOR" -lt 9 ]); then
        echo "  OpenCV < 4.9.0 may be vulnerable to CVE-2024-2167"
        echo ""
        echo "  Options:"
        echo "  1. Add PPA: sudo add-apt-repository ppa:deadsy/libopencv"
        echo "  2. Build from source: ./scripts/build-opencv-from-source.sh"
        echo "  3. Continue with warning (development only)"
        echo ""
        read -p "Continue with current OpenCV version? [y/N] " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            echo "  Aborting. Please update OpenCV and re-run this script."
            exit 1
        fi
    else
        echo "  OpenCV version is secure (>= 4.9.0)"
    fi
else
    echo "  Installing OpenCV..."
    apt install -y libopencv-dev
fi

# Install additional production dependencies
echo "[6/6] Installing additional dependencies..."
apt install -y \
    cmake \
    g++ \
    pkg-config \
    libcamera-dev \
    libpthread-stubs0-dev \
    libssl-dev \
    git

echo ""
echo "=== Setup Complete ==="
echo ""
echo "Next steps:"
echo "1. Rebuild AuroreMkVII: cd build-native && cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build . -j\$(nproc)"
echo "2. Run tests: ctest --output-on-failure"
echo "3. Deploy to Raspberry Pi 5: ./scripts/deploy-to-rpi.sh"
echo ""
echo "For Raspberry Pi 5 deployment, also run:"
echo "  sudo ./scripts/setup-rpi-realtime.sh"
echo ""
