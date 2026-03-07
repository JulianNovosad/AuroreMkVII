cmake_minimum_required(VERSION 3.20)
project(AuroreMkVI CXX)

set(CMAKE_BUILD_TYPE RelWithDebInfo)
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG -fPIC -Wall -Wextra")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG -fPIC -Wall -Wextra")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -mtune=native -Wall -Wextra")
# IMPORTANT: Sanitizers removed for compatibility (Thread and Address sanitizers are incompatible)
# Note: Removed -Werror to allow compilation with warnings

# Find required system packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBCAMERA REQUIRED libcamera)
find_package(OpenCV REQUIRED)

link_directories(${CMAKE_SOURCE_DIR}/artifacts/tflite/lib)
link_directories(${CMAKE_SOURCE_DIR}/artifacts/abseil/lib)

# Include directories for our source files
include_directories(
    ${CMAKE_SOURCE_DIR}/src
)

# System include directories for dependencies (warnings suppressed)
include_directories(SYSTEM
    ${CMAKE_SOURCE_DIR}/artifacts/libedgetpu
    ${CMAKE_SOURCE_DIR}/artifacts/tflite/include
    ${CMAKE_SOURCE_DIR}/deps/tensorflow
    ${LIBCAMERA_INCLUDE_DIRS}
    /usr/include/opencv4
    /usr/include/boost
    /usr/include/libdrm
)

link_directories(${CMAKE_SOURCE_DIR}/artifacts/tflite/lib)
link_directories(${CMAKE_SOURCE_DIR}/artifacts/abseil/lib)

# Add src subdirectory for modules
add_subdirectory(src)


