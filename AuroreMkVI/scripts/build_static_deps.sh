#!/bin/bash
set -euo pipefail

# Aurore Mk VI - Static Dependency Builder
# Builds TFLite and LibEdgeTPU as static libraries from local sources.

ROOT=$(pwd)
DEPS_DIR="${ROOT}/deps"
ARTIFACTS_DIR="${ROOT}/artifacts/tflite"
INCLUDE_DIR="${ROOT}/include"
JOBS=${JOBS:-2}
ARCH=$(uname -m)

echo "=== Aurore Mk VI Static Dependency Builder ==="
echo "ARCH: $ARCH"
echo "JOBS: $JOBS"
echo "ROOT: $ROOT"

# Ensure directories exist
mkdir -p "${ARTIFACTS_DIR}/lib"
mkdir -p "${INCLUDE_DIR}"

consolidate_headers() {
    echo "[PREP] Consolidating headers..."
    # TensorFlow
    mkdir -p "${INCLUDE_DIR}/tensorflow"
    rsync -av --delete "${DEPS_DIR}/tensorflow/tensorflow/" "${INCLUDE_DIR}/tensorflow/"
    
    # Abseil
    mkdir -p "${INCLUDE_DIR}/absl"
    rsync -av --delete "${DEPS_DIR}/abseil-cpp/absl/" "${INCLUDE_DIR}/absl/"
    
    # Flatbuffers
    mkdir -p "${INCLUDE_DIR}/flatbuffers"
    rsync -av --delete "${DEPS_DIR}/flatbuffers/include/flatbuffers/" "${INCLUDE_DIR}/flatbuffers/"
    
    # Eigen
    mkdir -p "${INCLUDE_DIR}/Eigen"
    rsync -av --delete "${DEPS_DIR}/eigen/Eigen/" "${INCLUDE_DIR}/Eigen/"
    
    # EdgeTPU (public headers)
    cp "${DEPS_DIR}/libedgetpu/api/edgetpu.h" "${INCLUDE_DIR}/" || cp "${DEPS_DIR}/libedgetpu/tflite/public/edgetpu.h" "${INCLUDE_DIR}/" || true
    cp "${DEPS_DIR}/libedgetpu/api/edgetpu_c.h" "${INCLUDE_DIR}/" || cp "${DEPS_DIR}/libedgetpu/tflite/public/edgetpu_c.h" "${INCLUDE_DIR}/" || true
    
    echo "Header consolidation complete."
}

build_tflite() {
    echo "[BUILD] TensorFlow Lite (Static)..."
    cd "${DEPS_DIR}/tensorflow"

    # Clean previous build to avoid source mismatch errors
    rm -rf build_static
    mkdir -p build_static
    cd build_static

    # Set FETCHCONTENT_SOURCE_DIR_* to force local sources
    cmake ../tensorflow/lite \
        -DFETCHCONTENT_SOURCE_DIR_TENSORFLOW="${DEPS_DIR}/tensorflow" \
        -DFETCHCONTENT_SOURCE_DIR_XNNPACK="${DEPS_DIR}/xnnpack" \
        -DFETCHCONTENT_SOURCE_DIR_CPUINFO="${DEPS_DIR}/cpuinfo" \
        -DFETCHCONTENT_SOURCE_DIR_RUY="${DEPS_DIR}/ruy" \
        -DFETCHCONTENT_SOURCE_DIR_FLATBUFFERS="${DEPS_DIR}/flatbuffers" \
        -DFETCHCONTENT_SOURCE_DIR_EIGEN="${DEPS_DIR}/eigen" \
        -DFETCHCONTENT_SOURCE_DIR_FARMHASH="${DEPS_DIR}/farmhash" \
        -DFETCHCONTENT_SOURCE_DIR_FFT2D="${DEPS_DIR}/fft2d" \
        -DFETCHCONTENT_SOURCE_DIR_ML_DTYPES="${DEPS_DIR}/ml_dtypes" \
        -DFETCHCONTENT_SOURCE_DIR_PTHREADPOOL="${DEPS_DIR}/pthreadpool" \
        -DFETCHCONTENT_SOURCE_DIR_FP16="${DEPS_DIR}/fp16" \
        -DFETCHCONTENT_SOURCE_DIR_FXDIV="${DEPS_DIR}/fxdiv" \
        -DFETCHCONTENT_SOURCE_DIR_KLEIDIAI="${DEPS_DIR}/kleidiai" \
        -DFETCHCONTENT_SOURCE_DIR_ABSEIL-CPP="${DEPS_DIR}/abseil-cpp" \
        -DFETCHCONTENT_SOURCE_DIR_ABSEIL_CPP="${DEPS_DIR}/abseil-cpp" \
        -DFETCHCONTENT_SOURCE_DIR_GEMMLOWP="${DEPS_DIR}/gemmlowp" \
        -DFETCHCONTENT_SOURCE_DIR_PROTOBUF="${DEPS_DIR}/protobuf" \
        -DXNNPACK_BUILD_ALL_MICROKERNELS=OFF \
        -DTFLITE_ENABLE_XNNPACK=ON \
        -DTFLITE_ENABLE_MMAP=ON \
        -DTFLITE_ENABLE_PROFILER=OFF \
        -DTFLITE_ENABLE_LABEL_IMAGE=OFF \
        -DTFLITE_ENABLE_BENCHMARK_MODEL=OFF \
        -DTFLITE_ENABLE_EXTERNAL_DELEGATE=ON \
        -DTFLITE_ENABLE_RUY=ON \
        -DTFLITE_ENABLE_INSTALL=ON \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_STANDARD=17 \
        -DBUILD_SHARED_LIBS=OFF \
        -Dtensorflow_ENABLE_XLA=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_INSTALL_PREFIX="${ARTIFACTS_DIR}"

    cmake --build . -j"${JOBS}"
    cmake --install .

    cd "${ROOT}"
}


}


build_libedgetpu() {
    echo "[BUILD] LibEdgeTPU (Static)..."
    cd "${DEPS_DIR}/libedgetpu/makefile_build"

    # Set TFROOT and BUILDROOT for the makefile
    make -f Makefile \
        TFROOT="${DEPS_DIR}/tensorflow" \
        BUILDROOT="../" \
        -j"${JOBS}" \
        libedgetpu

    # Verify and Move Artifacts
    if [ -f "../out/direct/${ARCH}/libedgetpu.a" ]; then
        echo "LibEdgeTPU build successful."
        cp "../out/direct/${ARCH}/libedgetpu.a" "${ARTIFACTS_DIR}/lib/"
    else
        echo "ERROR: LibEdgeTPU build failed. Artifact ../out/direct/${ARCH}/libedgetpu.a not found."
        exit 1
    fi
    cd "${ROOT}"
}

verify_artifacts() {
    echo "[VERIFY] Checking symbols..."
    local TFLITE_A="${ARTIFACTS_DIR}/lib/libtensorflow-lite.a"
    if [ ! -f "$TFLITE_A" ]; then
        TFLITE_A="${ARTIFACTS_DIR}/lib64/libtensorflow-lite.a"
    fi
    
    if [ ! -f "$TFLITE_A" ]; then
        echo "ERROR: libtensorflow-lite.a not found in $ARTIFACTS_DIR/lib or lib64"
        exit 1
    fi

    if nm -C "$TFLITE_A" | grep -q "BuiltinOpResolver"; then
        echo "✓ TFLite contains BuiltinOpResolver"
    else
        echo "✗ TFLite missing BuiltinOpResolver"
        exit 1
    fi

    if nm -C "${ARTIFACTS_DIR}/lib/libedgetpu.a" | grep -q "edgetpu_create_delegate"; then
        echo "✓ LibEdgeTPU contains edgetpu_create_delegate"
    else
        echo "✗ LibEdgeTPU missing edgetpu_create_delegate"
        exit 1
    fi
}

# Execution
consolidate_headers
# build_tflitebuild_libedgetpu
verify_artifacts

echo "Static dependency build complete."
