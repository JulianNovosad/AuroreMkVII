add_library(aurore_decision safety_monitor.cpp)
target_compile_options(aurore_decision PRIVATE -Werror)
target_include_directories(aurore_decision PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_link_libraries(aurore_decision PUBLIC aurore_telemetry)
