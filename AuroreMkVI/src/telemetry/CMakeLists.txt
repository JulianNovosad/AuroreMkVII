add_library(aurore_telemetry telemetry.cpp)
target_compile_options(aurore_telemetry PRIVATE -Werror)
target_include_directories(aurore_telemetry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
