# CMake Toolchain File for Raspberry Pi 5 (aarch64)
# 
# Usage:
#   mkdir build-rpi && cd build-rpi
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/aarch64-rpi5-toolchain.cmake
#
# Prerequisites:
#   sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
#
# Note: For full cross-compilation with dependencies, you have two options:
#   1. Build natively on the Raspberry Pi 5 (recommended)
#   2. Set up a sysroot with ARM64 libraries (advanced)
#
# This toolchain is configured for option 1 - build scripts will use
# native compilation when cross-compile dependencies aren't available.
#

# Target system
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_SYSTEM_VERSION 5.10)  # Minimum kernel version for RPi 5

# Cross-compiler toolchain
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Search paths for target libraries and headers
# Note: These paths may not exist on x86_64 host systems
# The build will fall back to native compilation if cross-libs aren't found
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Compiler flags for ARMv8.2-A (Cortex-A76 on Raspberry Pi 5)
# Reference: https://developer.arm.com/documentation/102374/0101/
set(CMAKE_C_FLAGS_INIT "-march=armv8-a+fp+simd -mtune=cortex-a76")
set(CMAKE_CXX_FLAGS_INIT "-march=armv8-a+fp+simd -mtune=cortex-a76")

# Release build optimizations
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG -ffast-math")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -ffast-math")

# Debug build with debug info
set(CMAKE_C_FLAGS_DEBUG "-O0 -g3 -DDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -DDEBUG")

# Linker flags
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--as-needed")

# pkg-config for cross-compilation
set(ENV{PKG_CONFIG_LIBDIR} /usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig)
set(ENV{PKG_CONFIG_PATH} /usr/lib/aarch64-linux-gnu/pkgconfig)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type (default to Release)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "=== Cross-Compilation for Raspberry Pi 5 ===")
message(STATUS "Target System:     ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_VERSION}")
message(STATUS "Target Processor:  ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "C Compiler:        ${CMAKE_C_COMPILER}")
message(STATUS "C++ Compiler:      ${CMAKE_CXX_COMPILER}")
message(STATUS "C Flags:           ${CMAKE_C_FLAGS_INIT}")
message(STATUS "C++ Flags:         ${CMAKE_CXX_FLAGS_INIT}")
message(STATUS "Build Type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "")
message(STATUS "Note: If dependency packages aren't found, build natively on RPi 5")
message(STATUS "      or set up a sysroot with ARM64 libraries.")
message(STATUS "=============================================")
message(STATUS "")
