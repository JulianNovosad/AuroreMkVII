add_library(aurore_inference inference.cpp gpu_detector.cpp gles_utils.cpp)

find_package(OpenCV REQUIRED)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLESV3 REQUIRED libgles3)

target_include_directories(aurore_inference PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${OpenCV_INCLUDE_DIRS}
    /usr/include/libcamera_headers
    /usr/include/libdrm
    ${EGL_INCLUDE_DIRS}
    ${GLESV3_INCLUDE_DIRS}
)
target_include_directories(aurore_inference SYSTEM PRIVATE
    /usr/include/nlohmann
    ${PROJECT_SOURCE_DIR}/artifacts/tflite/include
    ${PROJECT_SOURCE_DIR}/artifacts/tflite/include/tensorflow/lite/c
    ${PROJECT_SOURCE_DIR}/artifacts/libedgetpu/include
)

target_link_libraries(aurore_inference PRIVATE
    ${EDGETPU_LIB_DIR}/libedgetpu.so
    ${TFLITE_LIB_DIR}/libtensorflow-lite.so
    ${EGL_LIBRARIES}
    ${GLESV3_LIBRARIES}
    ${OpenCV_LIBS}
)
