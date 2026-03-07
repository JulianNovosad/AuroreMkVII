#!/bin/bash
# scripts/build-opencv-from-source.sh
# Build OpenCV 4.9.0 from source with ARM NEON optimization
#
# Usage: ./scripts/build-opencv-from-source.sh [--jobs N]
# Default: --jobs 4

set -e

OPENCV_VERSION="4.9.0"
BUILD_DIR="/tmp/opencv-build"
INSTALL_PREFIX="/usr/local"
JOBS=4

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --jobs)
            JOBS="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

echo "=== Building OpenCV $OPENCV_VERSION from source ==="
echo "Build directory: $BUILD_DIR"
echo "Install prefix: $INSTALL_PREFIX"
echo "Parallel jobs: $JOBS"
echo ""

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Download OpenCV
if [ ! -d "opencv" ]; then
    echo "[1/4] Downloading OpenCV $OPENCV_VERSION..."
    wget -q https://github.com/opencv/opencv/archive/refs/tags/$OPENCV_VERSION.tar.gz -O opencv.tar.gz
    tar xzf opencv.tar.gz
    mv opencv-$OPENCV_VERSION opencv
    rm opencv.tar.gz
else
    echo "[1/4] OpenCV source already downloaded"
fi

# Download OpenCV contrib (for extra modules)
if [ ! -d "opencv_contrib" ]; then
    echo "     Downloading OpenCV contrib..."
    wget -q https://github.com/opencv/opencv_contrib/archive/refs/tags/$OPENCV_VERSION.tar.gz -O opencv_contrib.tar.gz
    tar xzf opencv_contrib.tar.gz
    mv opencv_contrib-$OPENCV_VERSION opencv_contrib
    rm opencv_contrib.tar.gz
else
    echo "     OpenCV contrib already downloaded"
fi

# Configure build
echo "[2/4] Configuring CMake..."
cd opencv
mkdir -p build
cd build

cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX \
    -DOPENCV_EXTRA_MODULES_PATH=../../opencv_contrib/modules \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_TESTS=OFF \
    -DBUILD_PERF_TESTS=OFF \
    -DBUILD_DOCS=OFF \
    -DWITH_TBB=ON \
    -DWITH_OPENMP=ON \
    -DWITH_IPP=OFF \
    -DWITH_EIGEN=ON \
    -DWITH_NEON=ON \
    -DENABLE_NEON=ON \
    -DWITH_V4L=ON \
    -DWITH_GTK=ON \
    -DBUILD_opencv_python3=OFF \
    -DOPENCV_GENERATE_PKGCONFIG=ON \
    -DCPU_BASELINE=NEON \
    -DCPU_DISPATCH="" \
    -DENABLE_FAST_MATH=ON \
    -DBUILD_SHARED_LIBS=ON

# Build
echo "[3/4] Building OpenCV (this may take 30-60 minutes)..."
cmake --build . -j$JOBS

# Install
echo "[4/4] Installing OpenCV..."
sudo cmake --install .

# Update library cache
echo "     Updating library cache..."
sudo ldconfig

# Verify installation
echo ""
echo "=== Verifying installation ==="
pkg-config --modversion opencv4
echo ""
echo "OpenCV $OPENCV_VERSION installed successfully to $INSTALL_PREFIX"
echo ""
echo "Add to PATH if needed:"
echo "  export PKG_CONFIG_PATH=$INSTALL_PREFIX/lib/pkgconfig:\$PKG_CONFIG_PATH"
echo ""
