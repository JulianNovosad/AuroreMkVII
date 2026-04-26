# cmake/AuroreTestHelpers.cmake
# Provides aurore_add_test() — single registration point for all CTest targets.
# See docs/superpowers/specs/2026-04-26-boost-test-migration-design.md

# Requires CMake >= 3.16 (SKIP_RETURN_CODE property)

if(NOT TARGET Boost::unit_test_framework)
    find_package(Boost 1.83 REQUIRED COMPONENTS unit_test_framework)
endif()

if(NOT TARGET Threads::Threads)
    find_package(Threads REQUIRED)
endif()

# ---------------------------------------------------------------------------
# aurore_add_test(
#   NAME             <target-name>
#   SOURCES          <file.cpp> [...]
#   [LIBS            <lib> [...]]
#   [LABELS          "<label>[;<label>...]"]
#   [TIMEOUT         <seconds>]       default: 60
#   [RESOURCE_LOCK   <lock> [...]]
#   [HARDWARE]        exit 77 → CTest SKIP
# )
# ---------------------------------------------------------------------------
function(aurore_add_test)
    set(options  HARDWARE)
    set(one      NAME TIMEOUT)
    set(multi    SOURCES LIBS LABELS RESOURCE_LOCK)
    cmake_parse_arguments(A "${options}" "${one}" "${multi}" "${ARGN}")

    if(A_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "aurore_add_test(${A_NAME}): unrecognised arguments: ${A_UNPARSED_ARGUMENTS}")
    endif()

    if(NOT A_NAME)
        message(FATAL_ERROR "aurore_add_test: NAME is required")
    endif()
    if(NOT A_SOURCES)
        message(FATAL_ERROR "aurore_add_test: SOURCES is required")
    endif()
    if(NOT A_TIMEOUT)
        set(A_TIMEOUT 60)
    endif()

    add_executable(${A_NAME} ${A_SOURCES})

    target_include_directories(${A_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )

    # Boost::unit_test_framework (CMake imported target) handles static vs.
    # dynamic link mode automatically — do NOT add BOOST_TEST_DYN_LINK manually.
    target_link_libraries(${A_NAME} PRIVATE
        Threads::Threads
        Boost::unit_test_framework
        ${A_LIBS}
    )

    add_test(
        NAME    ${A_NAME}
        COMMAND $<TARGET_FILE:${A_NAME}> --log_level=test_suite --report_level=detailed
    )

    set(_props TIMEOUT ${A_TIMEOUT})
    if(A_LABELS)        list(APPEND _props LABELS        "${A_LABELS}")        endif()
    if(A_RESOURCE_LOCK) list(APPEND _props RESOURCE_LOCK ${A_RESOURCE_LOCK}) endif()
    # HARDWARE tests exit with code 77 on absent hardware; CTest marks them SKIP,
    # not FAIL — distinguishing "rig not attached" from a logic regression.
    if(A_HARDWARE)      list(APPEND _props SKIP_RETURN_CODE 77)               endif()
    set_tests_properties(${A_NAME} PROPERTIES ${_props})
endfunction()
