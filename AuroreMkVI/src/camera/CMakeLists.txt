find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBCAMERA REQUIRED libcamera)
find_package(OpenCV REQUIRED)

add_library(aurore_camera camera_capture.cpp)
target_compile_options(aurore_camera PRIVATE -Werror)
target_link_libraries(aurore_camera PUBLIC ${LIBCAMERA_LIBRARIES} ${OpenCV_LIBS})
target_include_directories(aurore_camera PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src # For application.h
    ${LIBCAMERA_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
    /usr/include/libcamera_headers
    /usr/include/libdrm
    SYSTEM /usr/include # For system-installed edgetpu.h and others
    SYSTEM /usr/include/nlohmann # For json.hpp
    SYSTEM ${PROJECT_SOURCE_DIR}/artifacts/tflite/include
)
