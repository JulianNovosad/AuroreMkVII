# Aurore MkVII - Fire Control System
# CMakeLists.txt - Main build configuration
#
# Build instructions:
#   mkdir build && cd build
#   cmake .. -DCMAKE_BUILD_TYPE=Release
#   cmake --build . -j$(nproc)
#
# Requirements:
#   - CMake 3.16+
#   - GCC 12+ or clang 15+ (aarch64-linux-gnu for Raspberry Pi)
#   - libcamera-dev
#   - libopencv-dev (>= 4.5)
#   - Linux with PREEMPT_RT kernel (optional but recommended)

cmake_minimum_required(VERSION 3.16)

project(AuroreMkVII
    VERSION 0.1.0
    DESCRIPTION "Fire Control System - Real-time Vision and Actuation"
    LANGUAGES CXX
)

# Configure CTest to run serially (avoid CPU contention on laptop)
include(CTest)
set(CTEST_PARALLEL_LEVEL 1)

# ============================================================================
# Build Options
# ============================================================================

option(AURORE_ENABLE_NEON "Enable ARM NEON SIMD optimizations" ON)
option(AURORE_ENABLE_GPU "Enable VideoCore VII GPU acceleration" OFF)
if(AURORE_ENABLE_GPU)
    add_definitions(-DAURORE_USE_GPU)
endif()
option(AURORE_ENABLE_TESTS "Build unit tests" ON)
option(AURORE_ENABLE_COVERAGE "Enable code coverage" OFF)
option(AURORE_ENABLE_SANITIZERS "Enable sanitizers (debug only)" OFF)
option(AURORE_REALTIME "Enable real-time optimizations" ON)

# Removed AURORE_LAPTOP_BUILD as mocks are being permanently removed for target hardware.

# ============================================================================
# C++ Standard Configuration
# ============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# Compiler Flags
# ============================================================================

# Base optimization flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 -g -DNDEBUG")

# ARM-specific flags for ARMv8.2-A (Cortex-A72 on RPi 5, or generic ARMv8-A)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    message(STATUS "Building for ARM64 (ARMv8.2-A)")
    
    if(AURORE_ENABLE_NEON)
        # Use ARMv8.2-A with NEON and crypto extensions
        # Generic ARMv8-A flags for broader compatibility
        add_compile_options(-march=armv8-a+fp+simd)
        add_compile_definitions(AURORE_HAS_NEON AURORE_TARGET_AARCH64)
        message(STATUS "ARM NEON SIMD enabled")
    endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|x64")
    message(STATUS "Building for x86_64 (development/host)")
    add_compile_definitions(AURORE_HOST_X86_64)
    
    # Enable SSE/AVX on x86_64 for development testing
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-msse4.2 -mavx2)
    endif()
endif()

# Real-time flags
if(AURORE_REALTIME)
    add_compile_definitions(AURORE_REALTIME)
    
    # Link with pthreads real-time extensions
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lpthread -lrt")
    
    message(STATUS "Real-time optimizations enabled")
endif()

# Warning flags
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wcast-align
    -Wcast-qual
    -Wold-style-cast
    -Wunused
    -Woverloaded-virtual
    -Wnon-virtual-dtor
    -Wshadow
    -Wformat=2
    -Wnull-dereference
    -Wdouble-promotion
    -Werror
)

# Additional warnings for GCC
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(
        -Wduplicated-cond
        -Wduplicated-branches
        -Wlogical-op
        -Wuseless-cast
    )
endif()

# ============================================================================
# Sanitizers and Coverage (Debug Only)
# ============================================================================

if(AURORE_ENABLE_SANITIZERS)
    message(STATUS "Disabling sanitizers for Valgrind analysis.")
    # No compile or link options for sanitizers when disabled for Valgrind
endif()

if(AURORE_ENABLE_COVERAGE)
    message(STATUS "Enabling code coverage")
    add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
    add_link_options(--coverage)
endif()

# ============================================================================
# Security Hardening
# ============================================================================

add_compile_options(
    -fstack-protector-strong      # Stack canaries
    -fstack-clash-protection      # Stack clash protection
    -D_FORTIFY_SOURCE=2           # Buffer overflow protection
    -fPIE                         # Position-independent executable
)

# Control flow protection for x86_64
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|x64")
    add_compile_options(-fcf-protection)
endif()

add_link_options(
    -fPIE
    -pie
    -Wl,-z,relro,-z,now           # Full RELRO
    -Wl,-z,noexecstack            # Non-executable stack
    -Wl,-z,defs                   # No undefined symbols in shared libs
)

# ============================================================================
# Dependencies
# ============================================================================

# Find required packages
find_package(Threads REQUIRED)

# OpenSSL for HMAC-SHA256
find_package(OpenSSL REQUIRED)

# Protobuf is optional for laptop builds (only needed for AuroreLink server)
find_package(Protobuf REQUIRED)

# Generate C++ source from proto schema if Protobuf found
if(Protobuf_FOUND)
    set(PROTO_FILES ${CMAKE_SOURCE_DIR}/proto/aurore.proto)
    protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})
    add_compile_definitions(AURORE_HAS_PROTOBUF)
else()
    # Stub out proto variables
    set(PROTO_SRCS "")
    set(PROTO_HDRS "")
endif()

# Make generated headers visible to all targets
include_directories(${Protobuf_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})

# ONNX Runtime for YOLO26n inference
# Prefer the pip-installed ORT (fully self-contained, no separate libonnx.so.1
# dependency) over the Debian system package, which carries libonnx.so.1 as a
# split shared library.  Both the system ORT and libonnx.so.1 register ONNX
# op schemas via global constructors → duplicate-registration errors at startup.
# The pip ORT bundles ONNX internally so only one schema registration pass runs.
find_package(PkgConfig QUIET)
find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime/onnxruntime_cxx_api.h
    PATHS /usr/include /usr/local/include)
find_library(ONNXRUNTIME_LIB onnxruntime
    PATHS
        /usr/local/lib/python3.13/dist-packages/onnxruntime/capi   # pip ORT — self-contained
        /usr/lib/aarch64-linux-gnu                                  # Debian system ORT (fallback)
        /usr/local/lib
    NO_DEFAULT_PATH)
if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIB)
    message(STATUS "Found ONNX Runtime: ${ONNXRUNTIME_LIB}")
    set(ONNXRUNTIME_FOUND TRUE)
else()
    message(WARNING "ONNX Runtime not found - Yolo26Detector will not build")
    set(ONNXRUNTIME_FOUND FALSE)
endif()

# nlohmann/json for ConfigLoader
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    message(WARNING "nlohmann_json not found - ConfigLoader will not build")
endif()

# OpenCV - use pkg-config module 'opencv4'
# Security requirement: >= 4.9.0 to address CVE-2024-2167 (heap buffer overflow)
# and CVE-2024-3400 (DNN denial of service)
# For development, 4.6.0+ is allowed with warning. Production requires 4.9.0+.
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPENCV opencv4>=4.6.0)
if(NOT OPENCV_FOUND)
    message(FATAL_ERROR "OpenCV 4.6.0+ not found. Install libopencv-dev (>= 4.9.0 for production)")
endif()

# Warn if OpenCV version is below security threshold
if(OPENCV_VERSION VERSION_LESS 4.9.0)
    message(WARNING
        "OpenCV ${OPENCV_VERSION} is below the recommended security version 4.9.0.\n"
        "CVE-2024-2167 (heap buffer overflow) may affect this version.\n"
        "For DEVELOPMENT only. PRODUCTION deployment requires OpenCV >= 4.9.0.\n"
        "To build OpenCV 4.9.0 from source, see docs/dependencies.md"
    )
    add_compile_definitions(AURORE_OPENCV_UNSECURE)
endif()

# libcamera - only required for non-laptop builds
pkg_check_modules(LIBCAMERA REQUIRED libcamera>=0.2.0)

# libwebp - transitive dependency via OpenCV, checked explicitly for CVE-2023-4863
# Security requirement: >= 1.3.2 to address critical heap buffer overflow
pkg_check_modules(WEBP libwebp)
if(WEBP_FOUND)
    # Extract major.minor.patch from version string
    string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" WEBP_VERSION_MATCH "${WEBP_VERSION}")
    if(WEBP_VERSION_MATCH)
        set(WEBP_VERSION_MAJOR "${CMAKE_MATCH_1}")
        set(WEBP_VERSION_MINOR "${CMAKE_MATCH_2}")
        set(WEBP_VERSION_PATCH "${CMAKE_MATCH_3}")
        
        # Check for minimum secure version 1.3.2
        if(WEBP_VERSION_MAJOR LESS 1 OR 
           (WEBP_VERSION_MAJOR EQUAL 1 AND WEBP_VERSION_MINOR LESS 3) OR
           (WEBP_VERSION_MAJOR EQUAL 1 AND WEBP_VERSION_MINOR EQUAL 3 AND WEBP_VERSION_PATCH LESS 2))
            message(FATAL_ERROR 
                "libwebp ${WEBP_VERSION} is vulnerable to CVE-2023-4863 (critical heap overflow).\n"
                "Please upgrade to libwebp >= 1.3.2:\n"
                "  sudo apt update && sudo apt install libwebp7 libwebp-dev"
            )
        endif()
        message(STATUS "libwebp version ${WEBP_VERSION} (secure)")
    else()
        message(WARNING "Could not parse libwebp version, proceeding with caution")
    endif()
else()
    message(WARNING "libwebp not found as direct dependency; OpenCV may use bundled version")
endif()

# glibc version check for CVE-2024-2961 (iconv buffer overflow)
# Ubuntu 24.04 ships with glibc 2.39-1ubuntu3 or later which includes the fix
find_program(LDCONF_EXECUTABLE ldconfig)
if(LDCONF_EXECUTABLE)
    execute_process(
        COMMAND ${LDCONF_EXECUTABLE} -p
        OUTPUT_VARIABLE LDCONF_OUTPUT
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    # Check for glibc version via ldd
    execute_process(
        COMMAND ldd --version
        OUTPUT_VARIABLE LDD_VERSION_OUTPUT
        ERROR_VARIABLE LDD_VERSION_OUTPUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    message(STATUS "glibc: ${LDD_VERSION_OUTPUT}")
endif()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    SYSTEM PUBLIC ${OPENCV_INCLUDE_DIRS}
    SYSTEM PUBLIC ${LIBCAMERA_INCLUDE_DIRS}
)

# Link directories
link_directories(
    ${OPENCV_LIBRARY_DIRS}
    ${LIBCAMERA_LIBRARY_DIRS}
)

# ============================================================================
# Main Application
# ============================================================================

# Source files - conditional based on laptop build
set(FUSION_HAT_SRC src/drivers/fusion_hat.cpp)
set(CAMERA_WRAPPER_SRC src/drivers/camera_wrapper.cpp)
message(STATUS "Using real hardware drivers")

set(CAMERA_AUTH_SRC src/drivers/camera_auth.cpp)

set(AURORE_SOURCES
    # Main entry point
    src/main.cpp

    # Drivers (mock versions if AURORE_LAPTOP_BUILD=ON)
    ${CAMERA_WRAPPER_SRC}
    ${CAMERA_AUTH_SRC}
    ${FUSION_HAT_SRC}
    src/drivers/usb_camera.cpp
    src/drivers/laser_rangefinder.cpp

    # Vision pipeline
    src/vision/apriltag_detector.cpp
    src/vision/orb_detector.cpp
    src/vision/dual_camera_manager.cpp
    src/vision/sweep_pattern.cpp    # Tracking
    src/tracking/kcf_tracker.cpp

    # Actuation
    src/actuation/ballistic_solver.cpp
    src/actuation/gimbal_controller.cpp

    # State machine
    src/state_machine/state_machine.cpp

    # Safety
    src/safety/interlock_controller.cpp

    # Common utilities
    src/common/telemetry_writer.cpp
    src/common/hud_socket.cpp
    src/common/command_socket.cpp
    src/common/config_loader.cpp
    src/common/security.cpp
    src/common/spatial_resolver.cpp

    # Network
    src/network/aurore_link_server.cpp
    src/network/mjpeg_streamer.cpp

    # Protobuf generated sources
    ${PROTO_SRCS}
)

if(ONNXRUNTIME_FOUND)
    list(APPEND AURORE_SOURCES src/vision/yolo26_detector.cpp)
endif()


# Header files (for IDE integration)
set(AURORE_HEADERS
    include/aurore/ring_buffer.hpp
    include/aurore/timing.hpp
    include/aurore/safety_monitor.hpp
    include/aurore/camera_wrapper.hpp
    include/aurore/telemetry_writer.hpp
    include/aurore/telemetry_types.hpp
    include/aurore/interlock_controller.hpp
    include/aurore/fusion_hat.hpp
    include/aurore/aurore_link_server.hpp
)

if(ONNXRUNTIME_FOUND)
    list(APPEND AURORE_HEADERS include/aurore/yolo26_detector.hpp)
endif()

# Create executable
add_executable(aurore ${AURORE_SOURCES} ${AURORE_HEADERS})

# Target include directories
target_include_directories(aurore PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Link libraries
target_link_libraries(aurore PRIVATE
    Threads::Threads
    ${OPENCV_LIBRARIES}
    ${LIBCAMERA_LIBRARIES}
    ${Protobuf_LIBRARIES}
    OpenSSL::Crypto
    rt      # Real-time extensions (clock_nanosleep, etc.)
    pthread # POSIX threads
)

if(ONNXRUNTIME_FOUND)
    target_include_directories(aurore PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
    target_link_libraries(aurore PRIVATE ${ONNXRUNTIME_LIB})
    target_compile_definitions(aurore PRIVATE AURORE_HAS_ONNXRUNTIME)
    # Set RPATH so the binary resolves libonnxruntime.so.1 from the same
    # directory it was linked against (prevents system ORT being picked up
    # if the pip path was used above).
    get_filename_component(_ORT_RPATH "${ONNXRUNTIME_LIB}" DIRECTORY)
    set_target_properties(aurore PROPERTIES
        BUILD_RPATH  "${_ORT_RPATH}"
        INSTALL_RPATH "${_ORT_RPATH}")
endif()

# Target compile definitions
target_compile_definitions(aurore PRIVATE
    AURORE_VERSION="${PROJECT_VERSION}"
    AURORE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    AURORE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    AURORE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    AURORE_DEBUG_TIMING
)

# ============================================================================
# Unit Tests
# ============================================================================

if(AURORE_ENABLE_TESTS)
    enable_testing()
    include(cmake/AuroreTestHelpers.cmake)

    # -----------------------------------------------------------------------
    # Tier 0 — Fast unit tests (pure logic, no hardware, <30s each)
    # -----------------------------------------------------------------------

    aurore_add_test(NAME ring_buffer_test
        SOURCES tests/unit/ring_buffer_test.cpp
        LABELS  "tier0;fast;safety"
        TIMEOUT 60
    )

    aurore_add_test(NAME timing_test
        SOURCES tests/unit/timing_test.cpp
        LIBS    rt
        LABELS  "tier0;fast"
        TIMEOUT 60
    )

    aurore_add_test(NAME numeric_robustness_test
        SOURCES tests/unit/numeric_robustness_test.cpp
                src/actuation/ballistic_solver.cpp
                src/test_infrastructure.cpp
        LABELS  "tier0;fast"
        TIMEOUT 60
    )

    aurore_add_test(NAME memory_resource_test
        SOURCES tests/unit/memory_resource_test.cpp
                src/test_infrastructure.cpp
        LIBS    rt
        LABELS  "tier0;fast"
        TIMEOUT 60
    )

    # -----------------------------------------------------------------------
    # Tier 1 — Safety-critical unit tests
    # -----------------------------------------------------------------------

    aurore_add_test(NAME safety_monitor_test
        SOURCES tests/unit/safety_monitor_test.cpp
        LIBS    rt
        LABELS  "tier1;safety"
        TIMEOUT 60
    )

    aurore_add_test(NAME safety_monitor_fault_codes_test
        SOURCES tests/unit/test_safety_monitor_fault_codes.cpp
        LIBS    rt
        LABELS  "tier1;safety"
        TIMEOUT 60
    )

    aurore_add_test(NAME safety_stress_test
        SOURCES tests/unit/safety_stress_test.cpp
        LIBS    rt
        LABELS  "tier1;safety"
        TIMEOUT 120
    )

    aurore_add_test(NAME state_machine_test
        SOURCES tests/unit/state_machine_test.cpp
                src/state_machine/state_machine.cpp
        LABELS  "tier1;safety"
        TIMEOUT 30
    )

    aurore_add_test(NAME state_machine_transitions_test
        SOURCES tests/unit/test_state_machine_transitions.cpp
                src/state_machine/state_machine.cpp
        LABELS  "tier1;safety"
        TIMEOUT 60
    )

    aurore_add_test(NAME state_machine_stress_test
        SOURCES tests/unit/state_machine_stress_test.cpp
                src/state_machine/state_machine.cpp
        LABELS  "tier1;safety"
        TIMEOUT 60
    )

    aurore_add_test(NAME interlock_controller_test
        SOURCES tests/unit/interlock_controller_test.cpp
                src/safety/interlock_controller.cpp
                src/drivers/fusion_hat.cpp
                src/common/security.cpp
        LIBS    rt
        LABELS  "tier1;safety"
        TIMEOUT 15
    )

    aurore_add_test(NAME fault_containment_test
        SOURCES tests/unit/fault_containment_test.cpp
                src/state_machine/state_machine.cpp
                src/test_infrastructure.cpp
        LIBS    rt
        LABELS  "tier1;safety"
        TIMEOUT 120
    )

    aurore_add_test(NAME state_mode_integrity_test
        SOURCES tests/unit/state_mode_integrity_test.cpp
                src/state_machine/state_machine.cpp
                src/test_infrastructure.cpp
        LABELS  "tier1;safety;state"
        TIMEOUT 120
    )

    aurore_add_test(NAME hostile_input_test
        SOURCES tests/unit/hostile_input_test.cpp
                src/state_machine/state_machine.cpp
                src/test_infrastructure.cpp
        LABELS  "tier1;safety"
        TIMEOUT 60
    )

    aurore_add_test(NAME reset_recovery_test
        SOURCES tests/unit/reset_recovery_test.cpp
                src/state_machine/state_machine.cpp
                src/test_infrastructure.cpp
        LABELS  "tier1;safety"
        TIMEOUT 60
    )

    aurore_add_test(NAME concurrency_pathology_test
        SOURCES tests/unit/concurrency_pathology_test.cpp
                src/test_infrastructure.cpp
        LABELS  "tier1"
        TIMEOUT 120
    )

    aurore_add_test(NAME resource_exhaustion_test
        SOURCES tests/unit/resource_exhaustion_test.cpp
                src/test_infrastructure.cpp
        LABELS  "tier1"
        TIMEOUT 120
    )

    # -----------------------------------------------------------------------
    # Tier 1 — Pure logic unit tests (no hardware)
    # -----------------------------------------------------------------------

    aurore_add_test(NAME ballistics_test
        SOURCES tests/unit/ballistics_test.cpp
                src/actuation/ballistic_solver.cpp
        LABELS  "tier1"
        TIMEOUT 60
    )

    aurore_add_test(NAME ballistics_stress_test
        SOURCES tests/unit/ballistics_stress_test.cpp
                src/actuation/ballistic_solver.cpp
        LABELS  "tier1"
        TIMEOUT 60
    )

    aurore_add_test(NAME firmware_updater_test
        SOURCES tests/unit/firmware_updater_test.cpp
                src/common/security.cpp
        LIBS    OpenSSL::Crypto
        LABELS  "tier1"
        TIMEOUT 30
    )

    aurore_add_test(NAME gimbal_controller_test
        SOURCES tests/unit/gimbal_controller_test.cpp
                src/actuation/gimbal_controller.cpp
                src/common/security.cpp
        LABELS  "tier1"
        TIMEOUT 30
    )

    aurore_add_test(NAME geometry_test
        SOURCES tests/unit/geometry_test.cpp
                src/actuation/gimbal_controller.cpp
                src/common/security.cpp
        LIBS    OpenSSL::Crypto
        LABELS  "tier1"
        TIMEOUT 30
    )

    aurore_add_test(NAME main_thread_orchestration_test
        SOURCES tests/unit/test_main_thread_orchestration.cpp
        LIBS    rt
        LABELS  "tier1"
        TIMEOUT 60
    )

    aurore_add_test(NAME raw10_test
        SOURCES tests/unit/raw10_test.cpp
        LABELS  "tier1"
        TIMEOUT 60
    )

    aurore_add_test(NAME telemetry_writer_test
        SOURCES tests/unit/telemetry_writer_test.cpp
                src/common/telemetry_writer.cpp
        LIBS    OpenSSL::Crypto
        LABELS  "tier1"
        TIMEOUT 30
    )

    aurore_add_test(NAME hud_socket_test
        SOURCES tests/unit/hud_socket_test.cpp
                src/common/hud_socket.cpp
                src/state_machine/state_machine.cpp
        LIBS    OpenSSL::Crypto
        LABELS  "tier1"
        TIMEOUT 60
    )

    aurore_add_test(NAME hud_socket_stress_test
        SOURCES tests/unit/hud_socket_stress_test.cpp
                src/common/hud_socket.cpp
                src/common/security.cpp
                src/state_machine/state_machine.cpp
        LIBS    crypto ssl
        LABELS  "tier1"
        TIMEOUT 60
    )

    aurore_add_test(NAME sequence_validation_test
        SOURCES tests/unit/sequence_validation_test.cpp
                src/common/security.cpp
        LIBS    OpenSSL::Crypto
        LABELS  "tier1"
        TIMEOUT 30
    )

    aurore_add_test(NAME config_loader_test
        SOURCES tests/unit/config_loader_test.cpp
                src/common/config_loader.cpp
        LIBS    nlohmann_json::nlohmann_json
        LABELS  "tier1"
        TIMEOUT 15
    )

    aurore_add_test(NAME aurore_link_test
        SOURCES tests/unit/aurore_link_test.cpp
                src/network/aurore_link_server.cpp
                src/common/security.cpp
                ${PROTO_SRCS}
        LIBS    ${Protobuf_LIBRARIES} OpenSSL::Crypto
        LABELS  "tier1"
        TIMEOUT 30
    )

    target_include_directories(aurore_link_test PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR} ${Protobuf_INCLUDE_DIRS})

    aurore_add_test(NAME emergency_inhibit_test
        SOURCES tests/unit/test_emergency_inhibit.cpp
                src/network/aurore_link_server.cpp
                src/common/security.cpp
                ${PROTO_SRCS}
        LIBS    ${Protobuf_LIBRARIES} OpenSSL::Crypto
        LABELS  "tier1"
        TIMEOUT 30
    )

    target_include_directories(emergency_inhibit_test PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR} ${Protobuf_INCLUDE_DIRS})

    # -----------------------------------------------------------------------
    # Tier 2 — Real-time / temporal tests
    # -----------------------------------------------------------------------

    aurore_add_test(NAME temporal_consistency_test
        SOURCES tests/unit/temporal_consistency_test.cpp
                src/test_infrastructure.cpp
        LIBS    rt
        LABELS  "tier2;rt"
        TIMEOUT 120
    )

    aurore_add_test(NAME observability_test
        SOURCES tests/unit/observability_test.cpp
                src/test_infrastructure.cpp
        LIBS    rt
        LABELS  "tier2;rt"
        TIMEOUT 120
    )

    # -----------------------------------------------------------------------
    # Stress / full suite
    # -----------------------------------------------------------------------

    aurore_add_test(NAME core_stress_test
        SOURCES tests/unit/core_stress_test.cpp
                src/test_infrastructure.cpp
        LIBS    rt
        LABELS  "stress"
        TIMEOUT 120
    )

    aurore_add_test(NAME aurore_full_test_suite
        SOURCES tests/unit/safety/aurore_full_test_suite.cpp
                src/actuation/gimbal_controller.cpp
                src/common/security.cpp
        LIBS    rt OpenSSL::Crypto
        LABELS  "full_suite"
        TIMEOUT 300
    )

    # -----------------------------------------------------------------------
    # Parameterised safety matrix
    # -----------------------------------------------------------------------

    aurore_add_test(NAME estop_matrix_test
        SOURCES tests/unit/safety/parameterized/estop_matrix.cpp
        LIBS    rt
        LABELS  "safety;parameterized"
        TIMEOUT 120
    )

    aurore_add_test(NAME watchdog_timeout_matrix_test
        SOURCES tests/unit/safety/parameterized/watchdog_timeout_matrix.cpp
        LIBS    rt
        LABELS  "safety;parameterized;rt_required"
        TIMEOUT 300
    )

    # -----------------------------------------------------------------------
    # Tier 1 — Security / coupling
    # -----------------------------------------------------------------------

    aurore_add_test(NAME security_stress_test
        SOURCES tests/unit/test_security_stress.cpp
                ${CAMERA_WRAPPER_SRC}
                ${CAMERA_AUTH_SRC}
        LIBS    rt OpenSSL::Crypto ${OPENCV_LIBRARIES} ${LIBCAMERA_LIBRARIES}
        LABELS  "tier1;safety"
        TIMEOUT 120
        HARDWARE
        RESOURCE_LOCK CSI_CAMERA_0
    )

    target_include_directories(security_stress_test PRIVATE
        ${OPENCV_INCLUDE_DIRS} ${LIBCAMERA_INCLUDE_DIRS})

    aurore_add_test(NAME coupling_control_actuation_test
        SOURCES tests/unit/test_coupling_control_actuation.cpp
                src/drivers/fusion_hat.cpp
                src/safety/interlock_controller.cpp
                src/common/security.cpp
        LIBS    rt OpenSSL::Crypto
        LABELS  "tier1;safety"
        TIMEOUT 30
        HARDWARE
        RESOURCE_LOCK I2C_BUS_1
    )

    aurore_add_test(NAME gimbal_command_rate_test
        SOURCES tests/unit/test_gimbal_command_rate.cpp
        LIBS    rt
        LABELS  "tier1"
        TIMEOUT 30
    )

    add_custom_command(TARGET gimbal_command_rate_test POST_BUILD
        COMMAND sudo /usr/sbin/setcap 'cap_sys_nice=+ep' $<TARGET_FILE:gimbal_command_rate_test>
        COMMENT "Setting CAP_SYS_NICE on gimbal_command_rate_test"
    )

    aurore_add_test(NAME detection_rate_test
        SOURCES tests/unit/test_detection_rate.cpp
        LIBS    rt
        LABELS  "tier1"
        TIMEOUT 60
    )

    # -----------------------------------------------------------------------
    # Hardware tests (HARDWARE flag → SKIP_RETURN_CODE 77 if rig absent)
    # -----------------------------------------------------------------------

    aurore_add_test(NAME fusion_hat_test
        SOURCES tests/unit/fusion_hat_test.cpp
                ${FUSION_HAT_SRC}
        LABELS  "hardware"
        TIMEOUT 60
        HARDWARE
        RESOURCE_LOCK I2C_BUS_1
    )

    aurore_add_test(NAME gimbal_actuation_test
        SOURCES tests/hardware/gimbal_actuation_test.cpp
                src/drivers/fusion_hat.cpp
        LIBS    rt
        LABELS  "hardware"
        TIMEOUT 120
        HARDWARE
        RESOURCE_LOCK I2C_BUS_1
    )

    aurore_add_test(NAME boresight_convergence_test
        SOURCES tests/hardware/boresight_convergence_test.cpp
                src/drivers/laser_rangefinder.cpp
        LIBS    rt
        LABELS  "hardware"
        TIMEOUT 120
        HARDWARE
        RESOURCE_LOCK uart_ttyAMA0
    )

    aurore_add_test(NAME laser_rangefinder_test
        SOURCES tests/unit/laser_rangefinder_test.cpp
                src/drivers/laser_rangefinder.cpp
        LIBS    rt
        LABELS  "hardware"
        TIMEOUT 60
        HARDWARE
        RESOURCE_LOCK uart_ttyAMA0
    )

    aurore_add_test(NAME lrf_20_samples_test
        SOURCES tests/unit/lrf_20_samples_test.cpp
                src/drivers/laser_rangefinder.cpp
        LIBS    rt
        LABELS  "hardware"
        TIMEOUT 120
        HARDWARE
        RESOURCE_LOCK uart_ttyAMA0
    )

    aurore_add_test(NAME camera_wrapper_test
        SOURCES tests/unit/camera_wrapper_test.cpp
                ${CAMERA_WRAPPER_SRC}
                ${CAMERA_AUTH_SRC}
        LIBS    rt OpenSSL::Crypto ${OPENCV_LIBRARIES} ${LIBCAMERA_LIBRARIES}
        LABELS  "hardware"
        TIMEOUT 30
        HARDWARE
        RESOURCE_LOCK CSI_CAMERA_0
    )

    target_include_directories(camera_wrapper_test PRIVATE
        ${OPENCV_INCLUDE_DIRS} ${LIBCAMERA_INCLUDE_DIRS})

    aurore_add_test(NAME tracker_test
        SOURCES tests/unit/tracker_test.cpp
                src/tracking/kcf_tracker.cpp
                ${CAMERA_WRAPPER_SRC}
                ${CAMERA_AUTH_SRC}
        LIBS    rt OpenSSL::Crypto ${OPENCV_LIBRARIES} ${LIBCAMERA_LIBRARIES}
        LABELS  "hardware"
        TIMEOUT 60
        HARDWARE
        RESOURCE_LOCK CSI_CAMERA_0
    )

    target_include_directories(tracker_test PRIVATE
        ${OPENCV_INCLUDE_DIRS} ${LIBCAMERA_INCLUDE_DIRS})

    aurore_add_test(NAME detector_test
        SOURCES tests/unit/detector_test.cpp
                src/vision/apriltag_detector.cpp
        LIBS    rt OpenSSL::Crypto ${OPENCV_LIBRARIES}
        LABELS  "hardware"
        TIMEOUT 60
        HARDWARE
        RESOURCE_LOCK CSI_CAMERA_0
    )

    target_include_directories(detector_test PRIVATE ${OPENCV_INCLUDE_DIRS})

    aurore_add_test(NAME vision_pipeline_latency_test
        SOURCES tests/unit/test_vision_pipeline_latency.cpp
                ${CAMERA_WRAPPER_SRC}
                ${CAMERA_AUTH_SRC}
        LIBS    rt OpenSSL::Crypto ${OPENCV_LIBRARIES} ${LIBCAMERA_LIBRARIES}
        LABELS  "hardware"
        TIMEOUT 60
        HARDWARE
        RESOURCE_LOCK CSI_CAMERA_0
    )

    target_include_directories(vision_pipeline_latency_test PRIVATE
        ${OPENCV_INCLUDE_DIRS} ${LIBCAMERA_INCLUDE_DIRS})

    aurore_add_test(NAME gpu_acceleration_test
        SOURCES tests/unit/test_gpu_acceleration.cpp
                ${CAMERA_WRAPPER_SRC}
                ${CAMERA_AUTH_SRC}
        LIBS    rt OpenSSL::Crypto ${OPENCV_LIBRARIES} ${LIBCAMERA_LIBRARIES}
        LABELS  "optional;hardware"
        TIMEOUT 60
        HARDWARE
        RESOURCE_LOCK CSI_CAMERA_0
    )

    target_include_directories(gpu_acceleration_test PRIVATE
        ${OPENCV_INCLUDE_DIRS} ${LIBCAMERA_INCLUDE_DIRS})

    # -----------------------------------------------------------------------
    # Integration tests
    # -----------------------------------------------------------------------

    aurore_add_test(NAME safety_fault_injection_test
        SOURCES tests/integration/safety_fault_injection_test.cpp
                src/state_machine/state_machine.cpp
        LIBS    rt
        LABELS  "IntegrationTest"
        TIMEOUT 120
    )

    aurore_add_test(NAME thermal_dma_health_test
        SOURCES tests/integration/thermal_dma_health_test.cpp
                src/test_infrastructure.cpp
        LIBS    rt
        LABELS  "tier4;hil;hardware"
        TIMEOUT 120
        HARDWARE
    )

    aurore_add_test(NAME laser_validation_test
        SOURCES tests/integration/laser_validation_test.cpp
                src/drivers/laser_rangefinder.cpp
                src/state_machine/state_machine.cpp
                src/safety/interlock_controller.cpp
                src/drivers/fusion_hat.cpp
        LIBS    rt OpenSSL::Crypto
        LABELS  "IntegrationTest;hardware"
        TIMEOUT 60
        HARDWARE
        RESOURCE_LOCK uart_ttyAMA0
    )

    aurore_add_test(NAME integration_check
        SOURCES tests/hardware/integration_check.cpp
                src/drivers/laser_rangefinder.cpp
                src/drivers/usb_camera.cpp
        LIBS    rt ${OPENCV_LIBRARIES}
        LABELS  "hardware"
        TIMEOUT 60
        HARDWARE
        RESOURCE_LOCK uart_ttyAMA0
    )

    target_include_directories(integration_check PRIVATE ${OPENCV_INCLUDE_DIRS})

    # -----------------------------------------------------------------------
    # Timing / RT bench
    # -----------------------------------------------------------------------

    aurore_add_test(NAME aurore_timing_tests
        SOURCES tests/timing/wcet_measurement.cpp
                tests/timing/jitter_analysis.cpp
        LIBS    rt
        LABELS  "hardware"
        TIMEOUT 300
        HARDWARE
    )

    aurore_add_test(NAME rt_bench
        SOURCES tests/rt_bench.cpp
                src/actuation/ballistic_solver.cpp
        LABELS  "stress"
        TIMEOUT 30
    )

    # Convenience target: run all integration tests
    add_custom_target(run_integration_tests
        COMMAND ${CMAKE_CTEST_COMMAND} -L IntegrationTest --output-on-failure
        DEPENDS safety_fault_injection_test laser_validation_test
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running all integration tests"
    )

endif() # AURORE_ENABLE_TESTS

# ============================================================================
# Installation
# ============================================================================

install(TARGETS aurore
    RUNTIME DESTINATION bin
)

install(FILES config/config.json.example
    DESTINATION etc/aurore
)

install(DIRECTORY scripts/
    DESTINATION share/aurore/scripts
    USE_SOURCE_PERMISSIONS
)

# ============================================================================
# Custom Targets
# ============================================================================

# Run clang-tidy static analysis
find_program(CLANG_TIDY_EXE NAMES "clang-tidy" "clang-tidy-15")
if(CLANG_TIDY_EXE)
    message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
    
    add_custom_target(tidy
        COMMAND ${CLANG_TIDY_EXE}
            -p ${CMAKE_BINARY_DIR}
            -extra-arg=-Wno-unknown-warning-option
            src/main.cpp
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running clang-tidy static analysis"
    )
else()
    message(STATUS "clang-tidy not found - skipping static analysis target")
endif()

# Run format check
find_program(CLANG_FORMAT_EXE NAMES "clang-format" "clang-format-15")
if(CLANG_FORMAT_EXE)
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT_EXE}
            --style=file
            --dry-run
            --Werror
            ${AURORE_SOURCES} ${AURORE_HEADERS}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking code formatting"
    )
    
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE}
            --style=file
            -i
            ${AURORE_SOURCES} ${AURORE_HEADERS}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Formatting code"
    )
endif()

# Generate documentation (Doxygen)
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    configure_file(
        ${CMAKE_SOURCE_DIR}/docs/Doxyfile.in
        ${CMAKE_BINARY_DIR}/Doxyfile
        @ONLY
    )
    
    add_custom_target(docs
        ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
    )
endif()

# ============================================================================
# Diagnostic Tools (always built, no test framework dependency)
# ============================================================================

add_executable(lrf_analyzer
    tools/lrf_analyzer.cpp
)
target_link_libraries(lrf_analyzer PRIVATE Threads::Threads)

add_executable(hardware_verify
    src/drivers/fusion_hat/hardware_verify.cpp
    src/drivers/fusion_hat/fusion_hat_sensor.cpp
)
target_include_directories(hardware_verify PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(hardware_verify PRIVATE Threads::Threads)

add_executable(laser_verify
    src/drivers/laser_verify.cpp
    src/drivers/laser_rangefinder.cpp
)
target_include_directories(laser_verify PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(laser_verify PRIVATE Threads::Threads rt)

# End-to-end latency measurement (requires camera + tracker hardware)
add_executable(aurore_latency_measurement
    src/latency_measurement.cpp
    ${CAMERA_WRAPPER_SRC}
    ${CAMERA_AUTH_SRC}
    src/tracking/kcf_tracker.cpp
    src/common/config_loader.cpp
)
target_include_directories(aurore_latency_measurement PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${OPENCV_INCLUDE_DIRS}
    ${LIBCAMERA_INCLUDE_DIRS}
)
target_link_libraries(aurore_latency_measurement PRIVATE
    Threads::Threads
    ${OPENCV_LIBRARIES}
    ${LIBCAMERA_LIBRARIES}
    rt
    OpenSSL::Crypto
)

# ============================================================================
# Print Configuration Summary
# ============================================================================

message(STATUS "")
message(STATUS "=== Aurore MkVII Build Configuration ===")
message(STATUS "Version:           ${PROJECT_VERSION}")
message(STATUS "Build type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ compiler:      ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ standard:      ${CMAKE_CXX_STANDARD}")
message(STATUS "System processor:  ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "")
message(STATUS "Options:")
message(STATUS "  NEON SIMD:       ${AURORE_ENABLE_NEON}")
message(STATUS "  GPU acceleration: ${AURORE_ENABLE_GPU}")
message(STATUS "  Real-time:       ${AURORE_REALTIME}")
message(STATUS "  Tests:           ${AURORE_ENABLE_TESTS}")
message(STATUS "  Sanitizers:      ${AURORE_ENABLE_SANITIZERS}")
message(STATUS "  Coverage:        ${AURORE_ENABLE_COVERAGE}")
message(STATUS "")
message(STATUS "Dependencies:")
message(STATUS "  OpenCV:          ${OPENCV_VERSION}")
message(STATUS "  libcamera:       ${LIBCAMERA_VERSION}")
message(STATUS "========================================")
message(STATUS "")
