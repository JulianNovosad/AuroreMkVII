add_library(aurore_servo servo.cpp servo_hardware.cpp pca9685_controller.cpp)
target_compile_options(aurore_servo PRIVATE -Werror)
target_include_directories(aurore_servo PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src # Explicitly adding src/ for general headers like util_logging.h
    ${LIBCAMERA_INCLUDE_PARENT}
    /usr/include/libcamera_headers
)
target_link_libraries(aurore_servo PUBLIC aurore_telemetry aurore_decision)
