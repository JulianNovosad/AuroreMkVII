find_package(OpenCV REQUIRED)
add_library(aurore_monitor STATIC monitor.cpp)
target_include_directories(aurore_monitor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src # Explicitly adding src/ for general headers
    ${OpenCV_INCLUDE_DIRS}
    /usr/include/libcamera_headers
    /usr/include/libdrm
    SYSTEM /usr/include
    SYSTEM /usr/include/nlohmann # For json.hpp
    SYSTEM ${PROJECT_SOURCE_DIR}/artifacts/tflite/include
)