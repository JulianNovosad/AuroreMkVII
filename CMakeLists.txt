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
option(AURORE_ENABLE_GPU "Enable VideoCore VII GPU acceleration" ON)
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
find_package(PkgConfig QUIET)
find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime/onnxruntime_cxx_api.h
    PATHS /usr/include /usr/local/include)
find_library(ONNXRUNTIME_LIB onnxruntime
    PATHS /usr/lib/aarch64-linux-gnu /usr/local/lib)
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
endif()

# Target compile definitions
target_compile_definitions(aurore PRIVATE
    AURORE_VERSION="${PROJECT_VERSION}"
    AURORE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    AURORE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    AURORE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

# ============================================================================
# Unit Tests
# ============================================================================

if(AURORE_ENABLE_TESTS)
    enable_testing()

    # Ring buffer tests (standalone executable)
    add_executable(ring_buffer_test
        tests/unit/ring_buffer_test.cpp
    )

    target_include_directories(ring_buffer_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )

    target_link_libraries(ring_buffer_test PRIVATE
        Threads::Threads
    )

    add_test(NAME RingBufferTest COMMAND ring_buffer_test)
    set_tests_properties(RingBufferTest PROPERTIES TIMEOUT 60)

    # Timing tests (standalone executable)
    add_executable(timing_test
        tests/unit/timing_test.cpp
    )

    target_include_directories(timing_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )

    target_link_libraries(timing_test PRIVATE
        Threads::Threads
        rt
    )

    add_test(NAME TimingTest COMMAND timing_test)
    set_tests_properties(TimingTest PROPERTIES TIMEOUT 60)

    # Core Utility Stress tests
    add_executable(core_stress_test
        tests/unit/core_stress_test.cpp
    )
    target_include_directories(core_stress_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(core_stress_test PRIVATE Threads::Threads rt)
    add_test(NAME CoreStressTest COMMAND core_stress_test)
    set_tests_properties(CoreStressTest PROPERTIES TIMEOUT 120)

    # Safety monitor tests (standalone executable)
    add_executable(safety_monitor_test
        tests/unit/safety_monitor_test.cpp
    )

    target_include_directories(safety_monitor_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )

    target_link_libraries(safety_monitor_test PRIVATE
        Threads::Threads
        rt
    )

    add_test(NAME SafetyMonitorTest COMMAND safety_monitor_test)
    set_tests_properties(SafetyMonitorTest PROPERTIES TIMEOUT 60)

    # Safety monitor fault codes tests (comprehensive 24 fault code coverage)
    add_executable(safety_monitor_fault_codes_test
        tests/unit/test_safety_monitor_fault_codes.cpp
    )
    target_include_directories(safety_monitor_fault_codes_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(safety_monitor_fault_codes_test PRIVATE
        Threads::Threads
        rt
    )
    add_test(NAME SafetyMonitorFaultCodesTest COMMAND safety_monitor_fault_codes_test)
    set_tests_properties(SafetyMonitorFaultCodesTest PROPERTIES TIMEOUT 60)

    # Safety Stress tests
    add_executable(safety_stress_test
        tests/unit/safety_stress_test.cpp
    )
    target_include_directories(safety_stress_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(safety_stress_test PRIVATE Threads::Threads rt)
    add_test(NAME SafetyStressTest COMMAND safety_stress_test)
    set_tests_properties(SafetyStressTest PROPERTIES TIMEOUT 120)

    # State machine tests
    add_executable(state_machine_test
        tests/unit/state_machine_test.cpp
        src/state_machine/state_machine.cpp
    )
    target_include_directories(state_machine_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(state_machine_test PRIVATE Threads::Threads)
    add_test(NAME StateMachineTest COMMAND state_machine_test)
    set_tests_properties(StateMachineTest PROPERTIES TIMEOUT 30)

    # State machine transitions tests (comprehensive transition coverage)
    add_executable(state_machine_transitions_test
        tests/unit/test_state_machine_transitions.cpp
        src/state_machine/state_machine.cpp
    )
    target_include_directories(state_machine_transitions_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(state_machine_transitions_test PRIVATE
        Threads::Threads
    )
    add_test(NAME StateMachineTransitionsTest COMMAND state_machine_transitions_test)
    set_tests_properties(StateMachineTransitionsTest PROPERTIES TIMEOUT 60)

    # State Machine Stress tests
    add_executable(state_machine_stress_test
        tests/unit/state_machine_stress_test.cpp
        src/state_machine/state_machine.cpp
    )
    target_include_directories(state_machine_stress_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(state_machine_stress_test PRIVATE Threads::Threads)
    add_test(NAME StateMachineStressTest COMMAND state_machine_stress_test)
    set_tests_properties(StateMachineStressTest PROPERTIES TIMEOUT 60)

    # Main thread orchestration tests
    add_executable(main_thread_orchestration_test
        tests/unit/test_main_thread_orchestration.cpp
    )
    target_include_directories(main_thread_orchestration_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(main_thread_orchestration_test PRIVATE
        Threads::Threads
        rt
    )
    add_test(NAME MainThreadOrchestrationTest COMMAND main_thread_orchestration_test)
    set_tests_properties(MainThreadOrchestrationTest PROPERTIES TIMEOUT 60)

    # AprilTag detector tests
    add_executable(detector_test
        tests/unit/detector_test.cpp
        src/vision/apriltag_detector.cpp
    )
    target_include_directories(detector_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(detector_test PRIVATE Threads::Threads ${OPENCV_LIBRARIES} rt OpenSSL::Crypto)
    add_test(NAME DetectorTest COMMAND detector_test)
    set_tests_properties(DetectorTest PROPERTIES TIMEOUT 60)

    # KCF tracker tests
    add_executable(tracker_test
        tests/unit/tracker_test.cpp
        src/tracking/kcf_tracker.cpp
        ${CAMERA_WRAPPER_SRC}
        ${CAMERA_AUTH_SRC}
    )
    target_include_directories(tracker_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(tracker_test PRIVATE Threads::Threads ${OPENCV_LIBRARIES} ${LIBCAMERA_LIBRARIES} rt OpenSSL::Crypto)
    add_test(NAME TrackerTest COMMAND tracker_test)
    set_tests_properties(TrackerTest PROPERTIES TIMEOUT 60)

    # Ballistics solver tests
    add_executable(ballistics_test
        tests/unit/ballistics_test.cpp
        src/actuation/ballistic_solver.cpp
    )
    target_include_directories(ballistics_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(ballistics_test PRIVATE Threads::Threads)
    add_test(NAME BallisticsTest COMMAND ballistics_test)
    set_tests_properties(BallisticsTest PROPERTIES TIMEOUT 60)

    # Ballistics Stress tests
    add_executable(ballistics_stress_test
        tests/unit/ballistics_stress_test.cpp
        src/actuation/ballistic_solver.cpp
    )
    target_include_directories(ballistics_stress_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(ballistics_stress_test PRIVATE Threads::Threads)
    add_test(NAME BallisticsStressTest COMMAND ballistics_stress_test)
    set_tests_properties(BallisticsStressTest PROPERTIES TIMEOUT 60)


    # Gimbal controller tests
    add_executable(gimbal_controller_test
        tests/unit/gimbal_controller_test.cpp
        src/actuation/gimbal_controller.cpp
        src/common/security.cpp
    )
    target_include_directories(gimbal_controller_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(gimbal_controller_test PRIVATE Threads::Threads)
    add_test(NAME GimbalControllerTest COMMAND gimbal_controller_test)
    set_tests_properties(GimbalControllerTest PROPERTIES TIMEOUT 30)

    # Geometry tests
    add_executable(geometry_test
        tests/unit/geometry_test.cpp
        src/actuation/gimbal_controller.cpp
        src/common/security.cpp
    )
    target_include_directories(geometry_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(geometry_test PRIVATE Threads::Threads OpenSSL::Crypto)
    add_test(NAME GeometryTest COMMAND geometry_test)
    set_tests_properties(GeometryTest PROPERTIES TIMEOUT 30)

    # Fusion HAT+ I2C driver tests
    add_executable(fusion_hat_test
        tests/unit/fusion_hat_test.cpp
        ${FUSION_HAT_SRC}
    )
    target_include_directories(fusion_hat_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(fusion_hat_test PRIVATE Threads::Threads)
    add_test(NAME FusionHatTest COMMAND fusion_hat_test)
    set_tests_properties(FusionHatTest PROPERTIES TIMEOUT 60)

    # Gimbal Actuation HIL test
    add_executable(gimbal_actuation_test
        tests/hardware/gimbal_actuation_test.cpp
        src/drivers/fusion_hat.cpp
    )
    target_include_directories(gimbal_actuation_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(gimbal_actuation_test PRIVATE Threads::Threads rt)
    add_test(NAME GimbalActuationTest COMMAND gimbal_actuation_test)
    set_tests_properties(GimbalActuationTest PROPERTIES TIMEOUT 120)

    # Boresight Convergence Test (hardware required)
    add_executable(boresight_convergence_test
        tests/hardware/boresight_convergence_test.cpp
        src/drivers/laser_rangefinder.cpp
    )
    target_include_directories(boresight_convergence_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(boresight_convergence_test PRIVATE Threads::Threads rt)
    add_test(NAME BoresightConvergenceTest COMMAND boresight_convergence_test)
    set_tests_properties(BoresightConvergenceTest PROPERTIES TIMEOUT 120)


    # Coupling Control-Actuation tests
    add_executable(coupling_control_actuation_test
        tests/unit/test_coupling_control_actuation.cpp
        src/drivers/fusion_hat.cpp # Use real Fusion HAT
        src/safety/interlock_controller.cpp
        src/common/security.cpp
    )
    target_include_directories(coupling_control_actuation_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(coupling_control_actuation_test PRIVATE Threads::Threads rt OpenSSL::Crypto)
    add_test(NAME CouplingControlActuationTest COMMAND coupling_control_actuation_test)
    set_tests_properties(CouplingControlActuationTest PROPERTIES TIMEOUT 30)

    # Gimbal command rate test (AM7-L2-ACT-001)
    add_executable(gimbal_command_rate_test
        tests/unit/test_gimbal_command_rate.cpp
    )
    target_include_directories(gimbal_command_rate_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(gimbal_command_rate_test PRIVATE Threads::Threads rt)
    add_test(NAME GimbalCommandRateTest COMMAND gimbal_command_rate_test)
    set_tests_properties(GimbalCommandRateTest PROPERTIES TIMEOUT 30)

    # Grant CAP_SYS_NICE capability to allow setting SCHED_FIFO priority without sudo
    add_custom_command(TARGET gimbal_command_rate_test POST_BUILD
        COMMAND sudo /usr/sbin/setcap 'cap_sys_nice=+ep' $<TARGET_FILE:gimbal_command_rate_test>
        COMMENT "Setting CAP_SYS_NICE on gimbal_command_rate_test executable"
    )

    # Detection rate test framework (AM7-L2-VIS-004)
    add_executable(detection_rate_test
        tests/unit/test_detection_rate.cpp
    )
    target_include_directories(detection_rate_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(detection_rate_test PRIVATE Threads::Threads rt)
    add_test(NAME DetectionRateTest COMMAND detection_rate_test)
    set_tests_properties(DetectionRateTest PROPERTIES TIMEOUT 60)

    # HUD UNIX domain socket tests
    add_executable(hud_socket_test
        tests/unit/hud_socket_test.cpp
        src/common/hud_socket.cpp
        src/state_machine/state_machine.cpp
    )
    target_include_directories(hud_socket_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(hud_socket_test PRIVATE Threads::Threads OpenSSL::Crypto)
    add_test(NAME HudSocketTest COMMAND hud_socket_test)
    set_tests_properties(HudSocketTest PROPERTIES TIMEOUT 60)

    # HUD Socket Stress tests
    add_executable(hud_socket_stress_test
        tests/unit/hud_socket_stress_test.cpp
        src/common/hud_socket.cpp
        src/common/security.cpp
        src/state_machine/state_machine.cpp
    )
    target_include_directories(hud_socket_stress_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(hud_socket_stress_test PRIVATE Threads::Threads crypto ssl)
    add_test(NAME HudSocketStressTest COMMAND hud_socket_stress_test)
    set_tests_properties(HudSocketStressTest PROPERTIES TIMEOUT 60)

    add_executable(raw10_test
        tests/unit/raw10_test.cpp
    )
    target_include_directories(raw10_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(raw10_test PRIVATE Threads::Threads)
    add_test(NAME Raw10Test COMMAND raw10_test)
    set_tests_properties(Raw10Test PROPERTIES TIMEOUT 60)

endif()

if(AURORE_ENABLE_TESTS)
    add_executable(telemetry_writer_test
        tests/unit/telemetry_writer_test.cpp
        src/common/telemetry_writer.cpp
    )
    target_include_directories(telemetry_writer_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(telemetry_writer_test PRIVATE Threads::Threads OpenSSL::Crypto)
    add_test(NAME TelemetryWriterTest COMMAND telemetry_writer_test)
    set_tests_properties(TelemetryWriterTest PROPERTIES TIMEOUT 30)
endif()

if(AURORE_ENABLE_TESTS)
    add_executable(interlock_controller_test
        tests/unit/interlock_controller_test.cpp
        src/safety/interlock_controller.cpp
        src/drivers/fusion_hat.cpp # Use real Fusion HAT
        src/common/security.cpp
    )
    target_include_directories(interlock_controller_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(interlock_controller_test PRIVATE Threads::Threads rt)
    add_test(NAME InterlockControllerTest COMMAND interlock_controller_test)
    set_tests_properties(InterlockControllerTest PROPERTIES TIMEOUT 15)
endif()

if(AURORE_ENABLE_TESTS)
    add_executable(camera_wrapper_test
        tests/unit/camera_wrapper_test.cpp
        ${CAMERA_WRAPPER_SRC}
        ${CAMERA_AUTH_SRC}
    )
    target_include_directories(camera_wrapper_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${OPENCV_INCLUDE_DIRS}
        ${LIBCAMERA_INCLUDE_DIRS}
    )
    target_link_libraries(camera_wrapper_test PRIVATE
        Threads::Threads
        ${OPENCV_LIBRARIES}
        ${LIBCAMERA_LIBRARIES}
        rt
        OpenSSL::Crypto
    )
    add_test(NAME CameraWrapperTest COMMAND camera_wrapper_test)
    set_tests_properties(CameraWrapperTest PROPERTIES TIMEOUT 30)
endif()

if(AURORE_ENABLE_TESTS)
    # Frame authentication tests (ICD-001 / AM7-L2-SEC-001)
    add_executable(frame_authentication_test
        tests/unit/test_frame_authentication.cpp
        ${CAMERA_WRAPPER_SRC}
        ${CAMERA_AUTH_SRC}
    )
    target_include_directories(frame_authentication_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${OPENCV_INCLUDE_DIRS}
        ${LIBCAMERA_INCLUDE_DIRS}
    )
    target_link_libraries(frame_authentication_test PRIVATE
        Threads::Threads
        ${OPENCV_LIBRARIES}
        ${LIBCAMERA_LIBRARIES}
        rt
        OpenSSL::Crypto
    )
    add_test(NAME FrameAuthenticationTest COMMAND frame_authentication_test)
    set_tests_properties(FrameAuthenticationTest PROPERTIES TIMEOUT 60)
endif()

if(AURORE_ENABLE_TESTS)
    # Vision pipeline latency tests (AM7-L2-VIS-003)
    add_executable(vision_pipeline_latency_test
        tests/unit/test_vision_pipeline_latency.cpp
        ${CAMERA_WRAPPER_SRC}
        ${CAMERA_AUTH_SRC}
    )
    target_include_directories(vision_pipeline_latency_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${OPENCV_INCLUDE_DIRS}
        ${LIBCAMERA_INCLUDE_DIRS}
    )
    target_link_libraries(vision_pipeline_latency_test PRIVATE
        Threads::Threads
        ${OPENCV_LIBRARIES}
        ${LIBCAMERA_LIBRARIES}
        rt
        OpenSSL::Crypto
    )
    add_test(NAME VisionPipelineLatencyTest COMMAND vision_pipeline_latency_test)
    set_tests_properties(VisionPipelineLatencyTest PROPERTIES TIMEOUT 60)
endif()

if(AURORE_ENABLE_TESTS)
    # GPU acceleration tests (VideoCore VII)
    add_executable(gpu_acceleration_test
        tests/unit/test_gpu_acceleration.cpp
        ${CAMERA_WRAPPER_SRC}
        ${CAMERA_AUTH_SRC}
    )
    target_include_directories(gpu_acceleration_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${OPENCV_INCLUDE_DIRS}
        ${LIBCAMERA_INCLUDE_DIRS}
    )
    target_link_libraries(gpu_acceleration_test PRIVATE
        Threads::Threads
        ${OPENCV_LIBRARIES}
        ${LIBCAMERA_LIBRARIES}
        rt
        OpenSSL::Crypto
    )
    # GPU test is optional - skip if hardware unavailable
    add_test(NAME GPUAccelerationTest COMMAND gpu_acceleration_test)
    set_tests_properties(GPUAccelerationTest
        PROPERTIES
        TIMEOUT 60
        LABELS "Optional"
    )
endif()

if(AURORE_ENABLE_TESTS)
    # AuroreLink TCP server tests
    add_executable(aurore_link_test
        tests/unit/aurore_link_test.cpp
        src/network/aurore_link_server.cpp
        src/common/security.cpp
        ${PROTO_SRCS}
    )
    target_include_directories(aurore_link_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}
        ${Protobuf_INCLUDE_DIRS}
    )
    target_link_libraries(aurore_link_test PRIVATE
        Threads::Threads ${Protobuf_LIBRARIES} OpenSSL::Crypto
    )
    add_test(NAME AuroreLinkTest COMMAND aurore_link_test)
    set_tests_properties(AuroreLinkTest PROPERTIES TIMEOUT 30)

    # EMERGENCY_INHIBIT handler tests
    add_executable(emergency_inhibit_test
        tests/unit/test_emergency_inhibit.cpp
        src/network/aurore_link_server.cpp
        src/common/security.cpp
        ${PROTO_SRCS}
    )
    target_include_directories(emergency_inhibit_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}
        ${Protobuf_INCLUDE_DIRS}
    )
    target_link_libraries(emergency_inhibit_test PRIVATE
        Threads::Threads ${Protobuf_LIBRARIES} OpenSSL::Crypto
    )
    add_test(NAME EmergencyInhibitTest COMMAND emergency_inhibit_test)
    set_tests_properties(EmergencyInhibitTest PROPERTIES TIMEOUT 30)
endif()

if(AURORE_ENABLE_TESTS)
    # ConfigLoader tests
    add_executable(config_loader_test
        tests/unit/config_loader_test.cpp
        src/common/config_loader.cpp
    )
    target_include_directories(config_loader_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(config_loader_test PRIVATE Threads::Threads nlohmann_json::nlohmann_json)
    add_test(NAME ConfigLoaderTest COMMAND config_loader_test)
    set_tests_properties(ConfigLoaderTest PROPERTIES TIMEOUT 15)
endif()

if(AURORE_ENABLE_TESTS)
    # Sequence validation tests (RFC 1982)
    add_executable(sequence_validation_test
        tests/unit/sequence_validation_test.cpp
        src/common/security.cpp
    )
    target_include_directories(sequence_validation_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(sequence_validation_test PRIVATE Threads::Threads OpenSSL::Crypto)
    add_test(NAME SequenceValidationTest COMMAND sequence_validation_test)
    set_tests_properties(SequenceValidationTest PROPERTIES TIMEOUT 30)

    # Security stress tests (High-frequency / Race conditions)
    add_executable(security_stress_test
        tests/unit/test_security_stress.cpp
        ${CAMERA_WRAPPER_SRC}
        ${CAMERA_AUTH_SRC}
    )
    target_include_directories(security_stress_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${OPENCV_INCLUDE_DIRS}
        ${LIBCAMERA_INCLUDE_DIRS}
    )
    target_link_libraries(security_stress_test PRIVATE
        Threads::Threads
        ${OPENCV_LIBRARIES}
        ${LIBCAMERA_LIBRARIES}
        rt
        OpenSSL::Crypto
    )
    add_test(NAME SecurityStressTest COMMAND security_stress_test)
    set_tests_properties(SecurityStressTest PROPERTIES TIMEOUT 120)
endif()

# ============================================================================
# Integration Tests
# ============================================================================

if(AURORE_ENABLE_TESTS)
    # Timing/integration tests (require real-time kernel)
    add_executable(aurore_timing_tests
        tests/timing/wcet_measurement.cpp
        # tests/timing/jitter_analysis.cpp    # TODO: Implement
        # tests/timing/thread_sync_test.cpp   # TODO: Implement
    )

    target_include_directories(aurore_timing_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/tests
    )

    target_link_libraries(aurore_timing_tests PRIVATE
        Threads::Threads
        rt
    )

    # Mark as optional (requires hardware)
    add_test(NAME TimingIntegrationTest COMMAND aurore_timing_tests)
    set_tests_properties(TimingIntegrationTest
        PROPERTIES
        TIMEOUT 300
        LABELS "hardware"
    )

    # Ballistics real-time stress bench: 10s at 1kHz, checks WCET ≤5ms
    add_executable(rt_bench
        tests/rt_bench.cpp
        src/actuation/ballistic_solver.cpp
    )
    target_include_directories(rt_bench PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(rt_bench PRIVATE Threads::Threads)
    add_test(NAME BallisticsRTBench COMMAND rt_bench)
    set_tests_properties(BallisticsRTBench PROPERTIES TIMEOUT 30 LABELS "stress")

    # ========================================================================
    # Integration Test Suite - End-to-End Data Flow Tests
    # ========================================================================

    # Safety Fault Injection FMEA Test
    # Tests: Fault detection, response, and FMEA coverage
    add_executable(safety_fault_injection_test
        tests/integration/safety_fault_injection_test.cpp
        src/state_machine/state_machine.cpp
    )

    target_include_directories(safety_fault_injection_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/tests
    )

    target_link_libraries(safety_fault_injection_test PRIVATE
        Threads::Threads
        rt
    )

    add_test(NAME SafetyFaultInjectionTest COMMAND safety_fault_injection_test)
    set_tests_properties(SafetyFaultInjectionTest
        PROPERTIES
        TIMEOUT 120
        LABELS "IntegrationTest"
    )

    # ========================================================================
    # Laser Rangefinder Unit Test
    # ========================================================================

    add_executable(laser_rangefinder_test
        tests/unit/laser_rangefinder_test.cpp
        src/drivers/laser_rangefinder.cpp
    )
    target_include_directories(laser_rangefinder_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(laser_rangefinder_test PRIVATE Threads::Threads rt)
    add_test(NAME LaserRangefinderTest COMMAND laser_rangefinder_test)
    set_tests_properties(LaserRangefinderTest PROPERTIES TIMEOUT 60)

    # 20-sample distance measurement test
    add_executable(lrf_20_samples_test
        tests/unit/lrf_20_samples_test.cpp
        src/drivers/laser_rangefinder.cpp
    )
    target_include_directories(lrf_20_samples_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(lrf_20_samples_test PRIVATE Threads::Threads rt)

    # ========================================================================
    # Laser Validation Integration Test
    # ========================================================================

    add_executable(laser_validation_test
        tests/integration/laser_validation_test.cpp
        src/drivers/laser_rangefinder.cpp
        src/state_machine/state_machine.cpp
        src/safety/interlock_controller.cpp
        src/drivers/fusion_hat.cpp
    )
    target_include_directories(laser_validation_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(laser_validation_test PRIVATE Threads::Threads rt OpenSSL::Crypto)
    add_test(NAME LaserValidationTest COMMAND laser_validation_test)
    set_tests_properties(LaserValidationTest PROPERTIES TIMEOUT 60 LABELS "IntegrationTest")

    # ========================================================================
    # Hardware Integration Check
    # ========================================================================

    add_executable(integration_check
        tests/hardware/integration_check.cpp
        src/drivers/laser_rangefinder.cpp
        src/drivers/usb_camera.cpp
    )
    target_include_directories(integration_check PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${OPENCV_INCLUDE_DIRS}
    )
    target_link_libraries(integration_check PRIVATE Threads::Threads rt ${OPENCV_LIBRARIES})
    add_test(NAME IntegrationCheck COMMAND integration_check)
    set_tests_properties(IntegrationCheck PROPERTIES TIMEOUT 60)

    # ========================================================================
    # Integration Test Runner (runs all integration tests)
    # ========================================================================

    # Add a convenience target to run all integration tests
    add_custom_target(run_integration_tests
        COMMAND ${CMAKE_CTEST_COMMAND} -R IntegrationTest --output-on-failure
        DEPENDS
            vision_pipeline_integration_test
            actuation_timing_integration_test
            safety_fault_injection_test
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running all integration tests"
    )
endif()

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
