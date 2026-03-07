# Linking Error Fix for Aurore Mk VI

## Problem Summary

When building `libtensorflow-lite.so` from static archives and linking the executable,
you get undefined reference errors for Abseil symbols like:
- `absl::lts_20240722::Mutex::Lock()`
- `absl::lts_20240722::log_internal::LogMessage`
- `absl::lts_20240722::str_format_internal`

## Root Cause

**Abseil libraries are only available as shared libraries (.so), not static archives (.a)**

The original build script tried to use `--whole-archive` with Abseil `.a` files:
```bash
# This FAILS because no .a files exist
-Wl,--whole-archive
    $(find "$PWD/artifacts/abseil/lib" -name '*.a' | sort -u)
```

But Abseil was built as shared libraries:
```bash
$ ls artifacts/abseil/lib/
libabsl_base.so           # ← Only .so files exist
libabsl_strings.so
libabsl_synchronization.so
```

## Solution

### Option 1: Link Abseil Shared Libraries with Executable (Recommended)

Update `src/CMakeLists.txt` to link against Abseil shared libraries:

```cmake
target_link_libraries(AuroreMkVI
    ${CMAKE_SOURCE_DIR}/artifacts/tflite/lib/libtensorflow-lite.so
    ${CMAKE_SOURCE_DIR}/artifacts/abseil/lib/libabsl_base.so
    ${CMAKE_SOURCE_DIR}/artifacts/abseil/lib/libabsl_strings.so
    ${CMAKE_SOURCE_DIR}/artifacts/abseil/lib/libabsl_synchronization.so
    ${CMAKE_SOURCE_DIR}/artifacts/abseil/lib/libabsl_time.so
    ${CMAKE_SOURCE_DIR}/artifacts/abseil/lib/libabsl_status.so
    ${CMAKE_SOURCE_DIR}/artifacts/abseil/lib/libabsl_log_internal_message.so
    # ... other libraries
)
```

### Option 2: Bundle Abseil into TFLite Shared Library

Rebuild TFLite with Abseil as static libraries, then use:

```bash
g++ -shared -Wl,-soname,libtensorflow-lite.so \
    -o artifacts/tflite/lib/libtensorflow-lite.so \
    -Wl,--whole-archive \
        libtensorflow-lite.a \
        libabsl_*.a \
    -Wl,--no-whole-archive \
    -lm -lpthread -lrt -fPIC
```

## Changes Made

1. **Updated `src/CMakeLists.txt`**:
   - Added explicit linking to Abseil shared libraries
   - Maintained RPATH settings for runtime resolution

2. **Updated `CMakeLists.txt`**:
   - Added `link_directories()` for Abseil library path

## Verification

After applying the fix, verify the build:

```bash
# Clean and rebuild
rm -rf build
cmake -B build -S .
cmake --build build -j$(nproc)

# Check executable dependencies
ldd build/AuroreMkVI | grep -E '(tensorflow-lite|absl_)'

# Verify all symbols are resolved
nm -D build/AuroreMkVI | grep 'absl::lts_20240722' | head -5
```

Expected output should show defined symbols (`T`) rather than undefined (`U`).
