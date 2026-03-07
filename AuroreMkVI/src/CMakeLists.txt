# src/CMakeLists.txt
# Defines how to build the AuroreMkVI executable from source files in src/

# Find Vulkan SDK - NO LONGER NEEDED (using EGL/GLES2)
# find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GBM REQUIRED gbm) # For GBM
find_package(Threads REQUIRED) # For pthread

# Find glslangValidator for shader compilation - NO LONGER NEEDD
# find_program(GLSLANG_VALIDATOR glslangValidator)
# if(NOT GLSLANG_VALIDATOR)
#     message(FATAL_ERROR "glslangValidator not found! Please install it (e.g., sudo apt install glslang-tools).")
# endif()

# List all source files for the main executable
set(AURORE_SOURCES
    application.cpp
    application_supervisor.cpp
    config_loader.cpp
    camera/camera_capture.cpp
    monitor/monitor.cpp

    discovery_module.cpp
    drm_display.cpp        # Use DRM instead of fbdev for proper display control
    fbdev_display.cpp      # fbdev-based display fallback (currently active)
    frame_analyzer.cpp
    global_definitions.cpp
    gles_overlay.cpp       # OpenGL ES-based GPU overlay rendering
    image_processor.cpp
    inference/inference.cpp
    inference/gpu_compute.cpp
    inference/gles_utils.cpp
    keyboard_monitor.cpp
    logic.cpp
    main.cpp
    orientation_sensor.cpp
    queue_monitor.cpp
    system_monitor.cpp
    telemetry/telemetry.cpp
    util_logging.cpp
    servo/servo.cpp
    servo/servo_hardware.cpp
    servo/pca9685_controller.cpp
    decision/safety_monitor.cpp
    geometry_verification.cpp
    simple.vert # Add the GLSL source files to the sources for dependency tracking
    simple.frag
)

# Define shader output paths - NO LONGER NEEDED (using header-only shaders)
# set(VERT_SPV_HEADER_FILE_NAME "simple_vert.spv.h") # Filename for the generated header
# set(FRAG_SPV_HEADER_FILE_NAME "simple_frag.spv.h") # Filename for the generated header
# set(VERT_SPV_HEADER_PATH "${CMAKE_CURRENT_BINARY_DIR}/${VERT_SPV_HEADER_FILE_NAME}")
# set(FRAG_SPV_HEADER_PATH "${CMAKE_CURRENT_BINARY_DIR}/${FRAG_SPV_HEADER_FILE_NAME}")

# Add custom commands to compile GLSL to SPIR-V and embed into headers using glslangValidator -x - NO LONGER NEEDED
# add_custom_command(
#     OUTPUT ${VERT_SPV_HEADER_PATH}
#     COMMAND ${GLSLANG_VALIDATOR} -x -V "${CMAKE_CURRENT_SOURCE_DIR}/simple.vert" -o "${VERT_SPV_HEADER_PATH}" --variable-name vertShaderCode
#     DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/simple.vert"
#     VERBATIM
# )

# add_custom_command(
#     OUTPUT ${FRAG_SPV_HEADER_PATH}
#     COMMAND ${GLSLANG_VALIDATOR} -x -V "${CMAKE_CURRENT_SOURCE_DIR}/simple.frag" -o "${FRAG_SPV_HEADER_PATH}" --variable-name fragShaderCode
#     DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/simple.frag"
#     VERBATIM
# )

# Add the generated headers to the source list so CMake knows they are part of the build process
# and ensures they are generated before compilation. - NO LONGER NEEDED
# list(APPEND AURORE_SOURCES "${VERT_SPV_HEADER_PATH}")
# list(APPEND AURORE_SOURCES "${FRAG_SPV_HEADER_PATH}")

# Add the executable target
add_executable(AuroreMkVI ${AURORE_SOURCES})

# Add Vulkan includes for AuroreMkVI - NO LONGER NEEDED
# target_include_directories(AuroreMkVI PUBLIC
#     ${Vulkan_INCLUDE_DIRS}
#     ${GBM_INCLUDE_DIRS}
#     /usr/include # For GLM headers
#     ${CMAKE_CURRENT_BINARY_DIR} # So generated headers can be found by gpu_overlay.cpp
# )

# Use simpler include directories
target_include_directories(AuroreMkVI PUBLIC
    ${GBM_INCLUDE_DIRS}
    /usr/include # For GLM headers
    ${CMAKE_CURRENT_BINARY_DIR} # So generated headers can be found
)

# Link to necessary libraries
target_link_libraries(AuroreMkVI
    ${CMAKE_SOURCE_DIR}/artifacts/abseil/lib/libabsl_base.so
    ${CMAKE_SOURCE_DIR}/artifacts/abseil/lib/libabsl_strings.so
    ${CMAKE_SOURCE_DIR}/artifacts/abseil/lib/libabsl_synchronization.so
    ${CMAKE_SOURCE_DIR}/artifacts/abseil/lib/libabsl_time.so
    ${CMAKE_SOURCE_DIR}/artifacts/abseil/lib/libabsl_status.so
    ${CMAKE_SOURCE_DIR}/artifacts/abseil/lib/libabsl_log_internal_message.so
    ${CMAKE_SOURCE_DIR}/artifacts/abseil/lib/libabsl_log_internal_nullguard.so
    edgetpu
    ${OpenCV_LIBS}
    ${LIBCAMERA_LIBRARIES}
    pthread
    dl
    zmq
    GLESv2
    EGL
    gbm
    drm
    vulkan
    ${CMAKE_THREAD_LIBS_INIT} # For Threads
)

# Set output directory for the executable
set_target_properties(AuroreMkVI PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Set include directories for the target (useful if specific headers are only for this target)
target_include_directories(AuroreMkVI PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/artifacts/libedgetpu/include
)

# Set RPATH for development (optional, system ldconfig is primary method)
set_target_properties(AuroreMkVI PROPERTIES
    BUILD_RPATH "$ORIGIN/../artifacts/abseil/lib:$ORIGIN/../artifacts/libedgetpu/lib:$ORIGIN/../artifacts/xnnpack/lib"
    INSTALL_RPATH "$ORIGIN/../artifacts/abseil/lib:$ORIGIN/../artifacts/libedgetpu/lib:$ORIGIN/../artifacts/xnnpack/lib"
)

# Safety-Critical Module Unit Tests (C++17, no external dependencies)
add_executable(ballistic_safety_test ballistic_safety_test.cpp)
target_compile_features(ballistic_safety_test PRIVATE cxx_std_17)
target_include_directories(ballistic_safety_test PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Thread Affinity Test
add_executable(thread_affinity_test test_thread_affinity.cpp)
target_compile_features(thread_affinity_test PRIVATE cxx_std_17)
target_include_directories(thread_affinity_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(thread_affinity_test pthread)

# TPU Worker WCET Integration Test
add_executable(tpu_wcet_test test_tpu_worker_wcet.cpp)
target_compile_features(tpu_wcet_test PRIVATE cxx_std_17)
target_include_directories(tpu_wcet_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tpu_wcet_test pthread)

# Per-Frame Watchdog Test
add_executable(watchdog_test test_per_frame_watchdog.cpp per_frame_watchdog.cpp)
target_compile_features(watchdog_test PRIVATE cxx_std_17)
target_include_directories(watchdog_test PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Geometry Verification Test
add_executable(geometry_test test_geometry_verification.cpp geometry_verification.cpp)
target_compile_features(geometry_test PRIVATE cxx_std_17)
target_include_directories(geometry_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(geometry_test m)

# DMA-BUF Pool Test
add_executable(dmabuf_pool_test test_dmabuf_pool.cpp util_logging.cpp config_loader.cpp global_definitions.cpp)
target_compile_features(dmabuf_pool_test PRIVATE cxx_std_17)
target_include_directories(dmabuf_pool_test PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Orange Zone Calculation Test
add_executable(orange_zone_test test_orange_zone.cpp geometry_verification.cpp)
target_compile_features(orange_zone_test PRIVATE cxx_std_17)
target_include_directories(orange_zone_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(orange_zone_test m)

# Target Tracking and Ballistic Prediction Test
add_executable(trajectory_tracking_test test_ballistics.cpp)
target_compile_features(trajectory_tracking_test PRIVATE cxx_std_17)
target_include_directories(trajectory_tracking_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(trajectory_tracking_test m pthread)

# Trigger Sanity Check Test
add_executable(trigger_sanity_test servo/test_trigger_sanity_check.cpp servo/trigger_sanity_check.cpp)
target_compile_features(trigger_sanity_test PRIVATE cxx_std_17)
target_include_directories(trigger_sanity_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(trigger_sanity_test m pthread)

# Overlay Bounding Box Test
add_executable(overlay_bbox_test test_overlay_bbox.cpp)
target_compile_features(overlay_bbox_test PRIVATE cxx_std_17)
target_include_directories(overlay_bbox_test PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Diagonal Fill Test
add_executable(diagonal_fill_test test_diagonal_fill.cpp)
target_compile_features(diagonal_fill_test PRIVATE cxx_std_17)
target_include_directories(diagonal_fill_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(diagonal_fill_test m)

# Overlay Queue Test
add_executable(overlay_queue_test test_overlay_queue.cpp)
target_compile_features(overlay_queue_test PRIVATE cxx_std_17)
target_include_directories(overlay_queue_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(overlay_queue_test pthread)

# WCET Analysis Test
add_executable(wcet_analysis test_wcet_analysis.cpp)
target_compile_features(wcet_analysis PRIVATE cxx_std_17)
target_include_directories(wcet_analysis PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(wcet_analysis m)

# Jitter Analysis Test
add_executable(jitter_analysis test_jitter_analysis.cpp)
target_compile_features(jitter_analysis PRIVATE cxx_std_17)
target_include_directories(jitter_analysis PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(jitter_analysis m pthread)

# Priority Inversion Detection Test
add_executable(priority_inversion_test test_priority_inversion.cpp)
target_compile_features(priority_inversion_test PRIVATE cxx_std_17)
target_include_directories(priority_inversion_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(priority_inversion_test pthread)

# Security Audit Test
add_executable(security_audit test_security_audit.cpp)
target_compile_features(security_audit PRIVATE cxx_std_17)
target_include_directories(security_audit PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(security_audit pthread)

# Numerical Stability Analysis Test
add_executable(numerical_stability_test test_numerical_stability.cpp)
target_compile_features(numerical_stability_test PRIVATE cxx_std_17)
target_include_directories(numerical_stability_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(numerical_stability_test m)

# Hardware Interface Audit Test
add_executable(hardware_interface_test test_hardware_interface.cpp)
target_compile_features(hardware_interface_test PRIVATE cxx_std_17)
target_include_directories(hardware_interface_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(hardware_interface_test pthread)

# Fail-Safe Verification Test
add_executable(fail_safe_verification test_fail_safe_verification.cpp)
target_compile_features(fail_safe_verification PRIVATE cxx_std_17)
target_include_directories(fail_safe_verification PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fail_safe_verification pthread)

# Human Factors Analysis Test
add_executable(human_factors_test test_human_factors.cpp)
target_compile_features(human_factors_test PRIVATE cxx_std_17)
target_include_directories(human_factors_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(human_factors_test pthread)

# Formal Verification Test
add_executable(formal_verification_test test_formal_verification.cpp)
target_compile_features(formal_verification_test PRIVATE cxx_std_17)
target_include_directories(formal_verification_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(formal_verification_test m pthread)

# Log Analysis Test
add_executable(log_analysis_test test_log_analysis.cpp)
target_compile_features(log_analysis_test PRIVATE cxx_std_17)
target_include_directories(log_analysis_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(log_analysis_test pthread)

# Evidence Compilation Test
add_executable(evidence_compilation_test test_evidence_compilation.cpp)
target_compile_features(evidence_compilation_test PRIVATE cxx_std_17)
target_include_directories(evidence_compilation_test PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Retest and Validation Test
add_executable(retest_validation_test test_retest_validation.cpp)
target_compile_features(retest_validation_test PRIVATE cxx_std_17)
target_include_directories(retest_validation_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(retest_validation_test pthread)

# Visual Pipeline Verification Test
add_executable(visual_pipeline_test test_visual_pipeline.cpp)
target_compile_features(visual_pipeline_test PRIVATE cxx_std_17)
target_include_directories(visual_pipeline_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(visual_pipeline_test m)

# Reliability Tracker MTBF Test
add_executable(reliability_test test_reliability_tracker.cpp)
target_compile_features(reliability_test PRIVATE cxx_std_17)
target_include_directories(reliability_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(reliability_test pthread)

# Thermal Shutdown Controller Test
add_executable(thermal_shutdown_test test_thermal_shutdown.cpp)
target_compile_features(thermal_shutdown_test PRIVATE cxx_std_17)
target_include_directories(thermal_shutdown_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(thermal_shutdown_test pthread)

# Stress Test Suite
add_executable(stress_test_suite test_stress_suite.cpp)
target_compile_features(stress_test_suite PRIVATE cxx_std_17)
target_include_directories(stress_test_suite PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(stress_test_suite pthread m)

# GPU Target Detection Test
add_executable(gpu_detection_test inference/test_gpu_detection.cpp inference/gpu_detector.cpp inference/gles_utils.cpp util_logging.cpp config_loader.cpp global_definitions.cpp)
target_compile_features(gpu_detection_test PRIVATE cxx_std_17)
target_include_directories(gpu_detection_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/inference
    /usr/include/libdrm
    ${EGL_INCLUDE_DIRS}
    ${GLESV3_INCLUDE_DIRS}
)
target_link_libraries(gpu_detection_test PRIVATE
    pthread
    EGL
    GLESv2
    ${OpenCV_LIBS}
)

# GPU Live Camera Test
add_executable(gpu_live_test inference/test_gpu_live.cpp inference/gpu_detector.cpp inference/gles_utils.cpp util_logging.cpp config_loader.cpp global_definitions.cpp)
target_compile_features(gpu_live_test PRIVATE cxx_std_17)
target_include_directories(gpu_live_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/inference
    /usr/include/libdrm
    ${EGL_INCLUDE_DIRS}
    ${GLESV3_INCLUDE_DIRS}
)
target_link_libraries(gpu_live_test PRIVATE
    pthread
    EGL
    GLESv2
    ${OpenCV_LIBS}
)

# Display Tests - Updated to use DrmDisplay
add_executable(test_display_only_feed test_display_only_feed.cpp drm_display.cpp util_logging.cpp config_loader.cpp camera/camera_capture.cpp queue_monitor.cpp global_definitions.cpp)
target_compile_features(test_display_only_feed PRIVATE cxx_std_17)
target_include_directories(test_display_only_feed PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/camera
)
target_link_libraries(test_display_only_feed PRIVATE
    pthread
    ${OpenCV_LIBS}
    ${LIBCAMERA_LIBRARIES}
    dl
    gbm
    drm
)

add_executable(test_display_only_overlay test_display_only_overlay.cpp drm_display.cpp util_logging.cpp config_loader.cpp queue_monitor.cpp global_definitions.cpp)
target_compile_features(test_display_only_overlay PRIVATE cxx_std_17)
target_include_directories(test_display_only_overlay PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_display_only_overlay PRIVATE
    pthread
    dl
    gbm
    drm
)

add_executable(test_display_both test_display_both.cpp drm_display.cpp camera/camera_capture.cpp util_logging.cpp config_loader.cpp queue_monitor.cpp global_definitions.cpp)
target_compile_features(test_display_both PRIVATE cxx_std_17)
target_include_directories(test_display_both PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/camera
)
target_link_libraries(test_display_both PRIVATE
    pthread
    ${OpenCV_LIBS}
    ${LIBCAMERA_LIBRARIES}
    dl
    gbm
    drm
)