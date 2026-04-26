# Boost.Test Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the hand-rolled `TEST()`/`RUN_TEST()`/`ASSERT_*` macro harness across 61 test files with Boost.Test 1.83, and reduce `CMakeLists.txt` from ~1,400 lines to ~450 by introducing `aurore_add_test()`.

**Architecture:** A new CMake helper module (`cmake/AuroreTestHelpers.cmake`) provides `aurore_add_test()`, which encapsulates all boilerplate. Test source files are converted wave-by-wave, building and running `ctest` at each checkpoint. No mocks, no test doubles, no behaviour changes — conversion is structural only.

**Tech Stack:** C++17, Boost.Test 1.83 (`/usr/include/boost/test/unit_test.hpp`), CMake 3.16+, CTest

---

## File Structure

| Action | Path | Responsibility |
|---|---|---|
| Create | `cmake/AuroreTestHelpers.cmake` | `aurore_add_test()` function |
| Modify | `CMakeLists.txt` | Replace 58 manual registrations with `aurore_add_test()` calls |
| Create (temp) | `tests/unit/skip_code_verification_test.cpp` | Verifies `std::_Exit(77)` → CTest SKIP; deleted after Task 6 |
| Modify | `tests/unit/ring_buffer_test.cpp` | Wave 2 |
| Modify | `tests/unit/safety_monitor_test.cpp` | Wave 2 |
| Modify | `tests/unit/state_machine_test.cpp` | Wave 2 |
| Modify | `tests/unit/interlock_controller_test.cpp` | Wave 2 |
| Modify | `tests/unit/timing_test.cpp` | Wave 3 |
| Modify | `tests/unit/ballistics_test.cpp` | Wave 3 |
| Modify | `tests/unit/ballistics_stress_test.cpp` | Wave 3 |
| Modify | `tests/unit/gimbal_controller_test.cpp` | Wave 3 |
| Modify | `tests/unit/geometry_test.cpp` | Wave 3 |
| Modify | `tests/unit/firmware_updater_test.cpp` | Wave 3 |
| Modify | `tests/unit/raw10_test.cpp` | Wave 3 |
| Modify | `tests/unit/config_loader_test.cpp` | Wave 3 |
| Modify | `tests/unit/sequence_validation_test.cpp` | Wave 3 |
| Modify | `tests/unit/test_state_machine_transitions.cpp` | Wave 3 |
| Modify | `tests/unit/state_machine_stress_test.cpp` | Wave 3 |
| Modify | `tests/unit/test_main_thread_orchestration.cpp` | Wave 3 |
| Modify | `tests/unit/aurore_link_test.cpp` | Wave 3 |
| Modify | `tests/unit/test_emergency_inhibit.cpp` | Wave 3 |
| Modify | `tests/unit/hud_socket_test.cpp` | Wave 3 |
| Modify | `tests/unit/hud_socket_stress_test.cpp` | Wave 3 |
| Modify | `tests/unit/telemetry_writer_test.cpp` | Wave 3 |
| Modify | `tests/unit/fusion_hat_test.cpp` | Wave 4 |
| Modify | `tests/unit/laser_rangefinder_test.cpp` | Wave 4 |
| Modify | `tests/unit/lrf_20_samples_test.cpp` | Wave 4 |
| Modify | `tests/integration/laser_validation_test.cpp` | Wave 4 |
| Modify | `tests/unit/camera_wrapper_test.cpp` | Wave 4 |
| Modify | `tests/unit/tracker_test.cpp` | Wave 4 |
| Modify | `tests/unit/detector_test.cpp` | Wave 4 |
| Modify | `tests/hardware/gimbal_actuation_test.cpp` | Wave 4 |
| Modify | `tests/hardware/boresight_convergence_test.cpp` | Wave 4 |
| Modify | `tests/unit/test_vision_pipeline_latency.cpp` | Wave 4 |
| Modify | `tests/hardware/integration_check.cpp` | Wave 4 |
| Modify | `tests/unit/core_stress_test.cpp` | Wave 5 |
| Modify | `tests/unit/safety_stress_test.cpp` | Wave 5 |
| Modify | `tests/unit/test_safety_monitor_fault_codes.cpp` | Wave 5 |
| Modify | `tests/unit/concurrency_pathology_test.cpp` | Wave 5 |
| Modify | `tests/unit/temporal_consistency_test.cpp` | Wave 5 |
| Modify | `tests/unit/numeric_robustness_test.cpp` | Wave 5 |
| Modify | `tests/unit/hostile_input_test.cpp` | Wave 5 |
| Modify | `tests/unit/resource_exhaustion_test.cpp` | Wave 5 |
| Modify | `tests/unit/reset_recovery_test.cpp` | Wave 5 |
| Modify | `tests/unit/observability_test.cpp` | Wave 5 |
| Modify | `tests/unit/memory_resource_test.cpp` | Wave 5 |
| Modify | `tests/unit/fault_containment_test.cpp` | Wave 5 |
| Modify | `tests/unit/state_mode_integrity_test.cpp` | Wave 5 |
| Modify | `tests/unit/safety/parameterized/estop_matrix.cpp` | Wave 5 |
| Modify | `tests/unit/safety/parameterized/watchdog_timeout_matrix.cpp` | Wave 5 |
| Modify | `tests/unit/safety/aurore_full_test_suite.cpp` | Wave 5 |
| Modify | `tests/unit/test_security_stress.cpp` | Wave 5 |
| Modify | `tests/unit/test_coupling_control_actuation.cpp` | Wave 5 |
| Modify | `tests/unit/test_gimbal_command_rate.cpp` | Wave 5 |
| Modify | `tests/unit/test_detection_rate.cpp` | Wave 5 |
| Modify | `tests/rt_bench.cpp` | Wave 5 |
| Modify | `tests/timing/wcet_measurement.cpp` | Wave 5 |
| Modify | `tests/timing/jitter_analysis.cpp` | Wave 5 |
| Modify | `tests/integration/safety_fault_injection_test.cpp` | Wave 5 |
| Modify | `tests/integration/thermal_dma_health_test.cpp` | Wave 5 |
| Modify | `tests/unit/test_gpu_acceleration.cpp` | Wave 5 |
| Modify | `CLAUDE.md` | Add Testing Workflow section |
| Delete (scan) | `src/**/*.cpp` orphans | MkVI residue not referenced in CMakeLists.txt |

---

## Boost.Test Conversion Rules (Reference)

Apply these rules mechanically to every test file in Waves 2–5.

### Rule A — Header transformation

**Remove:**
```cpp
#include <cassert>
#include <iostream>
// any hand-rolled macro block:
std::atomic<size_t> g_tests_run(0);
std::atomic<size_t> g_tests_passed(0);
std::atomic<size_t> g_tests_failed(0);
#define TEST(name) void name()
#define RUN_TEST(name) ...
#define ASSERT_TRUE(x) ...
#define ASSERT_FALSE(x) ...
#define ASSERT_EQ(a, b) ...
```

**Add at top:**
```cpp
#define BOOST_TEST_MODULE <ModuleNameTest>
#include <boost/test/unit_test.hpp>
```

Keep all other includes (`<thread>`, `<atomic>`, `<cstdint>`, etc.).

### Rule B — Test function transformation

| Before | After |
|---|---|
| `TEST(test_foo_bar) { ... }` | `BOOST_AUTO_TEST_CASE(foo_bar) { ... }` |
| `void test_foo_bar() { assert(...); }` | `BOOST_AUTO_TEST_CASE(foo_bar) { ... }` |
| `static bool test_foo_bar() { CHECK(...); return true; }` | `BOOST_AUTO_TEST_CASE(foo_bar) { ... }` |
| `ASSERT_TRUE(x)` | `BOOST_CHECK(x)` |
| `ASSERT_FALSE(x)` | `BOOST_CHECK(!x)` |
| `ASSERT_EQ(a, b)` | `BOOST_CHECK_EQUAL(a, b)` |
| `CHECK(cond, msg)` | `BOOST_CHECK_MESSAGE(cond, msg)` |
| `assert(cond)` | `BOOST_REQUIRE(cond)` |
| `std::cout << "PASS: ..." << std::endl;` | *(delete)* |
| `ASSERT_TRUE(x)` on preconditions | `BOOST_REQUIRE(x)` |

Wrap all test cases in a suite:
```cpp
BOOST_AUTO_TEST_SUITE(ModuleName)
// ... BOOST_AUTO_TEST_CASE entries ...
BOOST_AUTO_TEST_SUITE_END()
```

### Rule C — main() removal

Delete the entire `int main(...)` function. Boost.Test generates main() when `BOOST_TEST_MODULE` is defined.

Also delete: global test counters, `RUN_TEST` call lists, summary `std::cout` blocks, struct arrays of function pointers.

### Rule D — AURORE_LAPTOP_BUILD blocks

Any `#ifdef AURORE_LAPTOP_BUILD` / `#endif` block must be deleted entirely. The laptop build mode no longer exists.

### Rule E — Hardware detection (Wave 4 only)

For tests that open hardware (I2C, UART, camera), add a global fixture:
```cpp
#include <cstdlib>

struct <Peripheral>HardwareGuard {
    <Peripheral>HardwareGuard() {
        // probe the hardware
        if (!<probe expression>) {
            BOOST_TEST_MESSAGE("HARDWARE ABSENT: <peripheral>. Diagnose: <command>");
            std::_Exit(77);
        }
    }
};
BOOST_GLOBAL_FIXTURE(<Peripheral>HardwareGuard);
```

---

## Wave 1 — CMake Refactor

### Task 1: Write `cmake/AuroreTestHelpers.cmake`

**Files:**
- Create: `cmake/AuroreTestHelpers.cmake`

- [ ] **Step 1.1: Create the helper file**

```cmake
# cmake/AuroreTestHelpers.cmake
# Provides aurore_add_test() — single registration point for all CTest targets.
# See docs/superpowers/specs/2026-04-26-boost-test-migration-design.md

find_package(Boost 1.83 REQUIRED COMPONENTS unit_test_framework)

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
    cmake_parse_arguments(A "${options}" "${one}" "${multi}" ${ARGN})

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

    target_link_libraries(${A_NAME} PRIVATE
        Threads::Threads
        Boost::unit_test_framework
        ${A_LIBS}
    )

    add_test(
        NAME    ${A_NAME}
        COMMAND ${A_NAME} --log_level=test_suite --report_level=detailed
    )

    set(_props TIMEOUT ${A_TIMEOUT})
    if(A_LABELS)        list(APPEND _props LABELS        "${A_LABELS}")        endif()
    if(A_RESOURCE_LOCK) list(APPEND _props RESOURCE_LOCK "${A_RESOURCE_LOCK}") endif()
    if(A_HARDWARE)      list(APPEND _props SKIP_RETURN_CODE 77)               endif()
    set_tests_properties(${A_NAME} PROPERTIES ${_props})
endfunction()
```

- [ ] **Step 1.2: Verify the file exists**

```bash
ls -la /home/pi/AuroreMkVII/cmake/AuroreTestHelpers.cmake
```

Expected: file present, non-zero size.

---

### Task 2: Scan for orphaned source files in `src/`

**Files:**
- No file changes — investigation only

- [ ] **Step 2.1: Collect all .cpp files under src/**

```bash
find /home/pi/AuroreMkVII/src -name "*.cpp" | sort > /tmp/src_files.txt
cat /tmp/src_files.txt
```

- [ ] **Step 2.2: Collect all sources referenced in CMakeLists.txt**

```bash
grep -oP 'src/[^\s)]+\.cpp' /home/pi/AuroreMkVII/CMakeLists.txt | sort -u > /tmp/cmake_sources.txt
cat /tmp/cmake_sources.txt
```

- [ ] **Step 2.3: Find files in src/ but not in CMakeLists.txt**

```bash
comm -23 /tmp/src_files.txt \
     <(sed 's|^|/home/pi/AuroreMkVII/|' /tmp/cmake_sources.txt | sort)
```

Expected output: any `.cpp` files that exist but are never referenced. Review each one against `spec.md` and `AuroreMkVI/` to confirm it is MkVI residue, not an active stub with a commented-out TODO.

- [ ] **Step 2.4: Delete confirmed orphans**

For each confirmed orphan `src/path/to/file.cpp`:
```bash
git rm src/path/to/file.cpp
```

Do not delete any file that is referenced by a commented-out `add_executable` in CMakeLists.txt (those are future TODOs per spec.md).

---

### Task 3: Refactor `CMakeLists.txt` — replace test registrations

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 3.1: Add include of helper module**

In `CMakeLists.txt`, immediately after the line `enable_testing()` (around line 429), add:

```cmake
include(cmake/AuroreTestHelpers.cmake)
```

- [ ] **Step 3.2: Replace the entire test registration block**

Delete everything from `# Ring buffer tests (standalone executable)` (line ~432) through to the `endif()` closing the final `if(AURORE_ENABLE_TESTS)` block at line ~1313. Replace with the following complete block:

```cmake
if(AURORE_ENABLE_TESTS)

    # -----------------------------------------------------------------------
    # Tier 0 — Fast unit tests (pure logic, no hardware, <30s each)
    # -----------------------------------------------------------------------

    aurore_add_test(NAME ring_buffer_test
        SOURCES tests/unit/ring_buffer_test.cpp
        LABELS  "tier0;fast"
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
        TIMEOUT 120
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

    # lrf_20_samples_test: built but intentionally not registered with CTest
    # (used for manual interactive measurement sessions, not automated CI)
    add_executable(lrf_20_samples_test
        tests/unit/lrf_20_samples_test.cpp
        src/drivers/laser_rangefinder.cpp
    )
    target_include_directories(lrf_20_samples_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(lrf_20_samples_test PRIVATE
        Threads::Threads Boost::unit_test_framework rt)

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
        LABELS  "Optional;hardware"
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
```

> **Note:** The original `run_integration_tests` target referenced `vision_pipeline_integration_test` and `actuation_timing_integration_test` — neither exists. The replacement DEPENDS list above uses only the tests that actually exist.

- [ ] **Step 3.3: Verify no duplicate `enable_testing()` call remains**

```bash
grep -n "enable_testing" /home/pi/AuroreMkVII/CMakeLists.txt
```

Expected: exactly one occurrence (the existing one before the `include(cmake/AuroreTestHelpers.cmake)` line).

---

### Task 4: Build and run Wave 1 checkpoint

**Files:** No changes — verification only.

- [ ] **Step 4.1: Configure**

```bash
cd /home/pi/AuroreMkVII/build-rpi
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo 2>&1 | grep -E "error:|warning:|Boost"
```

Expected: `Found Boost 1.83...` with `unit_test_framework` listed. Zero CMake errors.

- [ ] **Step 4.2: Build all test targets**

```bash
cmake --build . -j$(nproc) --target all 2>&1 | tail -20
```

Expected: `[100%]` completion, zero build errors.

> Note: Individual test binaries will still pass/fail at runtime for correctness reasons (they haven't been converted yet). The goal here is that they **compile**.

- [ ] **Step 4.3: Confirm CTest sees the same test count as before**

```bash
ctest -N 2>&1 | tail -5
```

Expected: the same number of tests as before the CMake refactor (check the number against git: `git stash && cd build-rpi && cmake .. -q && ctest -N | tail -1 && git stash pop`).

- [ ] **Step 4.4: Commit Wave 1**

```bash
cd /home/pi/AuroreMkVII
git add cmake/AuroreTestHelpers.cmake CMakeLists.txt
git add -u src/  # picks up any deleted orphan files
git commit -m "refactor(cmake): introduce aurore_add_test() helper, eliminate test boilerplate

Reduces CMakeLists.txt test registration from ~1,000 lines to ~200.
All 58 tests re-registered via aurore_add_test() with HARDWARE flag and
RESOURCE_LOCK declarations. Orphaned src/ files from MkVI removed.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Wave 2 — Safety-Critical Tests + Skip Verification

### Task 5: Create skip-code verification test

This temporary test proves that `std::_Exit(77)` causes CTest to report SKIP, not FAIL, before any real hardware tests use this pattern.

**Files:**
- Create: `tests/unit/skip_code_verification_test.cpp`
- Modify: `CMakeLists.txt` (add one `aurore_add_test` call inside `if(AURORE_ENABLE_TESTS)`)

- [ ] **Step 5.1: Write the verification test**

```cpp
// tests/unit/skip_code_verification_test.cpp
// TEMPORARY — delete after Task 6 confirms SKIP behaviour.
#define BOOST_TEST_MODULE SkipCodeVerificationTest
#include <boost/test/unit_test.hpp>
#include <cstdlib>

struct AlwaysAbsentHardware {
    AlwaysAbsentHardware() {
        BOOST_TEST_MESSAGE(
            "HARDWARE ABSENT: skip_code_verification (intentional)");
        std::_Exit(77);
    }
};
BOOST_GLOBAL_FIXTURE(AlwaysAbsentHardware);

BOOST_AUTO_TEST_SUITE(SkipVerification)
BOOST_AUTO_TEST_CASE(should_never_run) {
    BOOST_FAIL("This line must never execute — fixture should have exited");
}
BOOST_AUTO_TEST_SUITE_END()
```

- [ ] **Step 5.2: Register it in CMakeLists.txt (inside `if(AURORE_ENABLE_TESTS)`)**

Add after the `rt_bench` registration:
```cmake
    aurore_add_test(NAME skip_code_verification_test
        SOURCES tests/unit/skip_code_verification_test.cpp
        LABELS  "tier0;skip_verify"
        TIMEOUT 10
        HARDWARE
    )
```

- [ ] **Step 5.3: Build**

```bash
cd /home/pi/AuroreMkVII/build-rpi
cmake --build . --target skip_code_verification_test 2>&1 | tail -5
```

Expected: builds cleanly.

---

### Task 6: Verify `std::_Exit(77)` → CTest SKIP

**Files:** No changes — verification only.

- [ ] **Step 6.1: Run the verification test with verbose output**

```bash
cd /home/pi/AuroreMkVII/build-rpi
ctest -R skip_code_verification_test -V
```

Expected output (key lines):
```
Test #N: skip_code_verification_test ...
  HARDWARE ABSENT: skip_code_verification (intentional)
N/N Test #N: skip_code_verification_test ...........Skipped   <seconds>
```

The word **Skipped** must appear. If the output shows **Failed** instead, the `SKIP_RETURN_CODE` property did not propagate correctly — re-examine `cmake/AuroreTestHelpers.cmake` and verify `set_tests_properties` runs after `add_test`.

- [ ] **Step 6.2: Confirm the exit code is 77 directly**

```bash
./skip_code_verification_test; echo "Exit code: $?"
```

Expected: `Exit code: 77`

- [ ] **Step 6.3: Confirm that a logic failure is FAIL (not SKIP)**

```bash
# Temporarily run ring_buffer_test before conversion — it uses the old harness
# but will exit 0 if the old tests still pass, or 1 if they fail.
./ring_buffer_test; echo "Exit code: $?"
```

Expected: `Exit code: 0` (old harness still passing). This confirms exit-code 0 = PASS and 77 = SKIP are distinct.

---

### Task 7: Remove skip-code verification test

- [ ] **Step 7.1: Delete the temporary test file**

```bash
git rm tests/unit/skip_code_verification_test.cpp
```

- [ ] **Step 7.2: Remove its registration from CMakeLists.txt**

Delete these 6 lines from `CMakeLists.txt`:
```cmake
    aurore_add_test(NAME skip_code_verification_test
        SOURCES tests/unit/skip_code_verification_test.cpp
        LABELS  "tier0;skip_verify"
        TIMEOUT 10
        HARDWARE
    )
```

- [ ] **Step 7.3: Rebuild to confirm clean**

```bash
cmake --build . -j$(nproc) 2>&1 | grep -E "error:|skip_code"
```

Expected: no build errors, no mention of `skip_code`.

---

### Task 8: Convert `ring_buffer_test.cpp`

**Files:**
- Modify: `tests/unit/ring_buffer_test.cpp`

Apply Conversion Rules A, B, C. The file has two sets of tests: `LockFreeRingBuffer` and `MPMCRingBuffer` — wrap each in its own `BOOST_AUTO_TEST_SUITE`.

- [ ] **Step 8.1: Replace the entire file**

```cpp
#define BOOST_TEST_MODULE RingBufferTest
#include <boost/test/unit_test.hpp>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "aurore/ring_buffer.hpp"

BOOST_AUTO_TEST_SUITE(LockFreeRingBuffer)

BOOST_AUTO_TEST_CASE(construction) {
    aurore::LockFreeRingBuffer<int, 4> buffer;
    BOOST_REQUIRE_EQUAL(buffer.capacity(), 4U);
    BOOST_REQUIRE_EQUAL(buffer.usable_capacity(), 3U);
    BOOST_CHECK(buffer.empty());
    BOOST_CHECK(!buffer.full());
    BOOST_CHECK_EQUAL(buffer.size(), 0U);
}

BOOST_AUTO_TEST_CASE(push_pop_single) {
    aurore::LockFreeRingBuffer<int, 4> buffer;
    BOOST_REQUIRE(buffer.push(42));
    BOOST_CHECK_EQUAL(buffer.size(), 1U);
    BOOST_CHECK(!buffer.empty());
    int value{};
    BOOST_REQUIRE(buffer.pop(value));
    BOOST_CHECK_EQUAL(value, 42);
    BOOST_CHECK_EQUAL(buffer.size(), 0U);
    BOOST_CHECK(buffer.empty());
}

BOOST_AUTO_TEST_CASE(push_pop_multiple) {
    aurore::LockFreeRingBuffer<int, 4> buffer;
    BOOST_REQUIRE(buffer.push(1));
    BOOST_REQUIRE(buffer.push(2));
    BOOST_REQUIRE(buffer.push(3));
    BOOST_CHECK_EQUAL(buffer.size(), 3U);
    BOOST_CHECK(buffer.full());
    BOOST_CHECK(!buffer.push(4));
    int value{};
    BOOST_REQUIRE(buffer.pop(value)); BOOST_CHECK_EQUAL(value, 1);
    BOOST_REQUIRE(buffer.pop(value)); BOOST_CHECK_EQUAL(value, 2);
    BOOST_REQUIRE(buffer.pop(value)); BOOST_CHECK_EQUAL(value, 3);
    BOOST_CHECK(buffer.empty());
}

BOOST_AUTO_TEST_CASE(wraparound) {
    aurore::LockFreeRingBuffer<int, 4> buffer;
    for (int i = 0; i < 10; ++i) {
        BOOST_REQUIRE(buffer.push(i));
        int value{};
        BOOST_REQUIRE(buffer.pop(value));
        BOOST_CHECK_EQUAL(value, i);
    }
    BOOST_CHECK(buffer.empty());
    BOOST_REQUIRE(buffer.push(100));
    BOOST_REQUIRE(buffer.push(200));
    int value{};
    BOOST_REQUIRE(buffer.pop(value)); BOOST_CHECK_EQUAL(value, 100);
    BOOST_REQUIRE(buffer.pop(value)); BOOST_CHECK_EQUAL(value, 200);
}

BOOST_AUTO_TEST_CASE(try_pop_optional) {
    aurore::LockFreeRingBuffer<int, 4> buffer;
    BOOST_CHECK(!buffer.try_pop().has_value());
    buffer.push(42);
    auto result = buffer.try_pop();
    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK_EQUAL(*result, 42);
}

BOOST_AUTO_TEST_CASE(spsc_stress) {
    constexpr size_t kNumElements = 100'000U;
    aurore::LockFreeRingBuffer<uint64_t, 256> buffer;
    std::atomic<uint64_t> produced{0};
    std::atomic<uint64_t> consumed{0};

    std::thread producer([&]() {
        for (uint64_t i = 0; i < kNumElements; ++i) {
            while (!buffer.push(i)) { std::this_thread::yield(); }
            produced.fetch_add(1U, std::memory_order_relaxed);
        }
    });

    std::thread consumer([&]() {
        uint64_t count = 0;
        while (count < kNumElements) {
            uint64_t value{};
            if (buffer.pop(value)) {
                ++count;
                consumed.fetch_add(1U, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();
    BOOST_CHECK_EQUAL(produced.load(), kNumElements);
    BOOST_CHECK_EQUAL(consumed.load(), kNumElements);
}

BOOST_AUTO_TEST_CASE(no_lost_elements) {
    constexpr size_t kNumElements = 10'000U;
    aurore::LockFreeRingBuffer<int, 16> buffer;
    std::atomic<int> sum_produced{0};
    std::atomic<int> sum_consumed{0};
    std::atomic<bool> done{false};

    std::thread producer([&]() {
        for (int i = 1; i <= static_cast<int>(kNumElements); ++i) {
            while (!buffer.push(i)) { std::this_thread::yield(); }
            sum_produced.fetch_add(i, std::memory_order_relaxed);
        }
        done.store(true, std::memory_order_release);
    });

    std::thread consumer([&]() {
        int value{};
        while (!done.load(std::memory_order_acquire) || !buffer.empty()) {
            if (buffer.pop(value)) {
                sum_consumed.fetch_add(value, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();
    BOOST_CHECK_EQUAL(sum_produced.load(), sum_consumed.load());
}

BOOST_AUTO_TEST_CASE(capacity_one) {
    aurore::LockFreeRingBuffer<int, 2> buffer;
    BOOST_CHECK_EQUAL(buffer.usable_capacity(), 1U);
    BOOST_REQUIRE(buffer.push(42));
    BOOST_CHECK(buffer.full());
    BOOST_CHECK(!buffer.push(99));
    int value{};
    BOOST_REQUIRE(buffer.pop(value));
    BOOST_CHECK_EQUAL(value, 42);
    BOOST_CHECK(buffer.empty());
}

BOOST_AUTO_TEST_CASE(large_buffer) {
    aurore::LockFreeRingBuffer<uint64_t, 4096> buffer;
    BOOST_CHECK_EQUAL(buffer.capacity(), 4096U);
    BOOST_CHECK_EQUAL(buffer.usable_capacity(), 4095U);
    for (uint64_t i = 0; i < 10'000U; ++i) {
        while (!buffer.push(i)) { std::this_thread::yield(); }
        uint64_t value{};
        while (!buffer.pop(value)) { std::this_thread::yield(); }
        BOOST_CHECK_EQUAL(value, i);
    }
}

BOOST_AUTO_TEST_CASE(struct_type) {
    struct TestData {
        uint64_t id;
        double   value;
        char     padding[64];
    };
    aurore::LockFreeRingBuffer<TestData, 8> buffer;
    TestData data{42U, 3.14159, {}};
    BOOST_REQUIRE(buffer.push(data));
    TestData popped{};
    BOOST_REQUIRE(buffer.pop(popped));
    BOOST_CHECK_EQUAL(popped.id, 42U);
    BOOST_CHECK_CLOSE(popped.value, 3.14159, 1e-9);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================

BOOST_AUTO_TEST_SUITE(MPMCRingBuffer)

BOOST_AUTO_TEST_CASE(basic) {
    aurore::MPMCRingBuffer<int, 4> buffer;
    BOOST_REQUIRE(buffer.push(1));
    BOOST_REQUIRE(buffer.push(2));
    BOOST_REQUIRE(buffer.push(3));
    int value{};
    BOOST_REQUIRE(buffer.pop(value)); BOOST_CHECK_EQUAL(value, 1);
    BOOST_REQUIRE(buffer.pop(value)); BOOST_CHECK_EQUAL(value, 2);
    BOOST_REQUIRE(buffer.pop(value)); BOOST_CHECK_EQUAL(value, 3);
}

BOOST_AUTO_TEST_CASE(concurrent) {
    constexpr size_t kNumElements = 10'000U;
    aurore::MPMCRingBuffer<int, 256> buffer;
    std::atomic<int> sum{0};
    std::vector<std::thread> producers;
    for (int p = 0; p < 4; ++p) {
        producers.emplace_back([&, p]() {
            for (int i = 0; i < static_cast<int>(kNumElements); ++i) {
                while (!buffer.push(p * 100'000 + i)) { std::this_thread::yield(); }
            }
        });
    }
    std::thread consumer([&]() {
        size_t count = 0;
        int value{};
        while (count < kNumElements * 4U) {
            if (buffer.pop(value)) {
                sum.fetch_add(value, std::memory_order_relaxed);
                ++count;
            } else {
                std::this_thread::yield();
            }
        }
    });
    for (auto& t : producers) { t.join(); }
    consumer.join();
    int expected{0};
    for (int p = 0; p < 4; ++p) {
        for (int i = 0; i < static_cast<int>(kNumElements); ++i) {
            expected += p * 100'000 + i;
        }
    }
    BOOST_CHECK_EQUAL(sum.load(), expected);
}

BOOST_AUTO_TEST_SUITE_END()
```

- [ ] **Step 8.2: Build and run**

```bash
cd /home/pi/AuroreMkVII/build-rpi
cmake --build . --target ring_buffer_test 2>&1 | tail -5
ctest -R ring_buffer_test -V
```

Expected: all test cases PASS. Zero failures.

- [ ] **Step 8.3: Commit**

```bash
cd /home/pi/AuroreMkVII
git add tests/unit/ring_buffer_test.cpp
git commit -m "test(wave2): convert ring_buffer_test to Boost.Test

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 9: Convert `safety_monitor_test.cpp`

**Files:**
- Modify: `tests/unit/safety_monitor_test.cpp`

The file has 23 test functions using the `TEST(name)/RUN_TEST/ASSERT_*` harness. Apply Rules A, B, C.

- [ ] **Step 9.1: Replace the file header (top 55 lines)**

Replace everything up to and including the closing `}  // anonymous namespace` with:

```cpp
#define BOOST_TEST_MODULE SafetyMonitorTest
#include <boost/test/unit_test.hpp>

#include "aurore/safety_monitor.hpp"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace {
void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
}  // namespace

BOOST_AUTO_TEST_SUITE(SafetyMonitor)
```

- [ ] **Step 9.2: Convert each test function**

For every function of the form:
```cpp
TEST(test_safety_monitor_X) {
    ASSERT_TRUE(expr);
    ASSERT_EQ(a, b);
}
```

Rewrite as:
```cpp
BOOST_AUTO_TEST_CASE(X) {
    BOOST_CHECK(expr);
    BOOST_CHECK_EQUAL(a, b);
}
```

The 23 functions to convert (strip `test_safety_monitor_` prefix for the case name):

| Old function name | New case name |
|---|---|
| `test_safety_monitor_construction` | `construction` |
| `test_safety_monitor_init` | `init` |
| `test_safety_monitor_start_stop` | `start_stop` |
| `test_safety_monitor_vision_update` | `vision_update` |
| `test_safety_monitor_actuation_update` | `actuation_update` |
| `test_safety_monitor_vision_latency_fault` | `vision_latency_fault` |
| `test_safety_monitor_emergency_stop` | `emergency_stop` |
| `test_safety_monitor_fault_clear` | `fault_clear` |
| `test_safety_monitor_safety_callback` | `safety_callback` |
| `test_safety_monitor_log_callback` | `log_callback` |
| `test_fault_code_to_string` | `fault_code_to_string` |
| `test_safety_monitor_rapid_updates` | `rapid_updates` |
| `test_safety_monitor_concurrent_access` | `concurrent_access` |
| `test_safety_event_structure` | `safety_event_structure` |
| `test_software_watchdog_construction` | `software_watchdog_construction` |
| `test_software_watchdog_init_starts_thread` | `software_watchdog_init_starts_thread` |
| `test_software_watchdog_kick_prevents_timeout` | `software_watchdog_kick_prevents_timeout` |
| `test_software_watchdog_timeout_triggers_fault` | `software_watchdog_timeout_triggers_fault` |
| `test_software_watchdog_disabled` | `software_watchdog_disabled` |
| `test_watchdog_kick_raii` | `watchdog_kick_raii` |
| `test_watchdog_kick_raii_multiple` | `watchdog_kick_raii_multiple` |
| `test_software_watchdog_config_defaults` | `software_watchdog_config_defaults` |
| `test_stage_latency_recording` | `stage_latency_recording` |
| `test_stage_stall_detection` | `stage_stall_detection` |
| `test_health_report_generation` | `health_report_generation` |
| `test_stage_stats_reset` | `stage_stats_reset` |

- [ ] **Step 9.3: Replace the `main()` with the suite closing**

Delete the entire `int main(...)` function and replace with:

```cpp
BOOST_AUTO_TEST_SUITE_END()
```

- [ ] **Step 9.4: Build and run**

```bash
cd /home/pi/AuroreMkVII/build-rpi
cmake --build . --target safety_monitor_test 2>&1 | tail -5
ctest -R safety_monitor_test -V
```

Expected: all test cases PASS.

- [ ] **Step 9.5: Commit**

```bash
cd /home/pi/AuroreMkVII
git add tests/unit/safety_monitor_test.cpp
git commit -m "test(wave2): convert safety_monitor_test to Boost.Test

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 10: Convert `state_machine_test.cpp`

**Files:**
- Modify: `tests/unit/state_machine_test.cpp`

This file uses standalone `void test_X()` functions with raw `assert()` and `std::cout` (different from ring_buffer/safety_monitor). Apply Rules A, B, C. Replace `assert(cond)` with `BOOST_REQUIRE(cond)`.

- [ ] **Step 10.1: Replace the file header**

Replace all includes and any top-level declarations before the first test function with:

```cpp
#define BOOST_TEST_MODULE StateMachineTest
#include <boost/test/unit_test.hpp>

#include "aurore/state_machine.hpp"
#include "aurore/timing.hpp"

#include <chrono>
#include <cmath>

using namespace aurore;

BOOST_AUTO_TEST_SUITE(StateMachine)
```

- [ ] **Step 10.2: List every test function in the file**

```bash
grep -n "^void test_" /home/pi/AuroreMkVII/tests/unit/state_machine_test.cpp
```

This lists every `void test_X()` function name and line number. For each one, apply the following transformation:

For every function of the form:
```cpp
void test_foo_bar() {
    StateMachine sm;
    assert(sm.state() == FcsState::BOOT);
    std::cout << "PASS: ...\n";
}
```

Rewrite as:
```cpp
BOOST_AUTO_TEST_CASE(foo_bar) {
    StateMachine sm;
    BOOST_REQUIRE(sm.state() == FcsState::BOOT);
}
```

Strip the `test_` prefix from each function name to get the `BOOST_AUTO_TEST_CASE` name. Replace `assert(x)` with `BOOST_REQUIRE(x)` and delete all `std::cout` lines.

- [ ] **Step 10.3: Remove `main()` and add suite close**

Delete `int main()` and the final `return` statement. Add:

```cpp
BOOST_AUTO_TEST_SUITE_END()
```

- [ ] **Step 10.4: Build and run**

```bash
cd /home/pi/AuroreMkVII/build-rpi
cmake --build . --target state_machine_test 2>&1 | tail -5
ctest -R state_machine_test -V
```

Expected: all test cases PASS.

- [ ] **Step 10.5: Commit**

```bash
cd /home/pi/AuroreMkVII
git add tests/unit/state_machine_test.cpp
git commit -m "test(wave2): convert state_machine_test to Boost.Test

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 11: Convert `interlock_controller_test.cpp`

**Files:**
- Modify: `tests/unit/interlock_controller_test.cpp`

> **Warning:** This file uses a completely different harness from the others — `static bool test_X()` returning bool, a local `CHECK(cond, msg)` macro that calls `return false`, and a struct-array dispatch in `main()`. The conversion maps `CHECK(cond, msg)` → `BOOST_CHECK_MESSAGE(cond, msg)`. Also delete the `#ifdef AURORE_LAPTOP_BUILD` block entirely.

- [ ] **Step 11.1: Replace the entire file**

```cpp
#define BOOST_TEST_MODULE InterlockControllerTest
#include <boost/test/unit_test.hpp>

#include "aurore/interlock_controller.hpp"

#include <cstdint>

using namespace aurore;

BOOST_AUTO_TEST_SUITE(InterlockController)

BOOST_AUTO_TEST_CASE(initial_state_is_unknown) {
    InterlockController ic(nullptr);
    BOOST_REQUIRE(ic.get_state() == InterlockState::UNKNOWN);
}

BOOST_AUTO_TEST_CASE(force_state_changes_state) {
    InterlockController ic(nullptr);
    ic.force_state(InterlockState::CLOSED);
    BOOST_CHECK_MESSAGE(ic.get_state() == InterlockState::CLOSED,
        "state should be CLOSED after force_state(CLOSED)");
    ic.force_state(InterlockState::OPEN);
    BOOST_CHECK_MESSAGE(ic.get_state() == InterlockState::OPEN,
        "state should be OPEN after force_state(OPEN)");
}

BOOST_AUTO_TEST_CASE(actuation_allowed_only_when_closed) {
    InterlockController ic(nullptr);
    ic.force_state(InterlockState::OPEN);
    BOOST_CHECK_MESSAGE(!ic.is_actuation_allowed(),
        "actuation should NOT be allowed when state is OPEN");
    ic.force_state(InterlockState::CLOSED);
    BOOST_CHECK_MESSAGE(ic.is_actuation_allowed(),
        "actuation SHOULD be allowed when state is CLOSED");
    ic.force_state(InterlockState::FAULT);
    BOOST_CHECK_MESSAGE(!ic.is_actuation_allowed(),
        "actuation should NOT be allowed when state is FAULT");
}

BOOST_AUTO_TEST_CASE(get_status_reflects_state) {
    InterlockController ic(nullptr);
    ic.force_state(InterlockState::CLOSED);
    {
        InterlockStatus s = ic.get_status();
        BOOST_CHECK_MESSAGE(s.state == InterlockState::CLOSED,
            "status.state should be CLOSED");
        BOOST_CHECK_MESSAGE(!s.actuation_inhibited,
            "actuation_inhibited should be false when CLOSED");
    }
    ic.force_state(InterlockState::FAULT);
    {
        InterlockStatus s = ic.get_status();
        BOOST_CHECK_MESSAGE(s.state == InterlockState::FAULT,
            "status.state should be FAULT");
        BOOST_CHECK_MESSAGE(s.actuation_inhibited,
            "actuation_inhibited should be true when FAULT");
    }
}

BOOST_AUTO_TEST_CASE(watchdog_feed_increments_counter) {
    InterlockConfig cfg;
    cfg.enable_watchdog = true;
    InterlockController ic(nullptr, cfg);
    const uint64_t before = ic.get_status().watchdog_feeds;
    ic.watchdog_feed();
    BOOST_CHECK_EQUAL(ic.get_status().watchdog_feeds, before + 1U);
}

BOOST_AUTO_TEST_SUITE_END()
```

- [ ] **Step 11.2: Build and run**

```bash
cd /home/pi/AuroreMkVII/build-rpi
cmake --build . --target interlock_controller_test 2>&1 | tail -5
ctest -R interlock_controller_test -V
```

Expected: 5 test cases PASS.

- [ ] **Step 11.3: Commit**

```bash
cd /home/pi/AuroreMkVII
git add tests/unit/interlock_controller_test.cpp
git commit -m "test(wave2): convert interlock_controller_test to Boost.Test

Removes AURORE_LAPTOP_BUILD conditional block (build mode removed).
Converts static-bool/CHECK harness to BOOST_CHECK_MESSAGE.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 12: Wave 2 checkpoint — verify safety label filtering

- [ ] **Step 12.1: Run safety-labelled tests only**

```bash
cd /home/pi/AuroreMkVII/build-rpi
ctest -L safety --output-on-failure -V 2>&1 | grep -E "Test #|PASS|FAIL|Skipped"
```

Expected: `ring_buffer_test`, `safety_monitor_test`, `state_machine_test`, `interlock_controller_test` all appear and all show `Passed`.

- [ ] **Step 12.2: Confirm no false positives**

```bash
ctest -L safety -N
```

Expected: the test count matches exactly the safety-labelled tests registered in CMakeLists.txt. If tests with no `LABELS` argument appear, re-check the `aurore_add_test()` calls for missing LABELS.

---

## Wave 3 — Pure Logic Unit Tests

### Task 13: Convert timing, ballistics, and geometry tests

**Files:**
- Modify: `tests/unit/timing_test.cpp`
- Modify: `tests/unit/ballistics_test.cpp`
- Modify: `tests/unit/ballistics_stress_test.cpp`
- Modify: `tests/unit/gimbal_controller_test.cpp`
- Modify: `tests/unit/geometry_test.cpp`

For each file, apply Rules A, B, C from the Conversion Rules section. Each file uses the same `TEST(name)/RUN_TEST/ASSERT_*` harness as `ring_buffer_test.cpp`. The conversion is mechanical:

1. Open the file.
2. Replace header (Rule A).
3. Convert each `TEST(test_<module>_X)` to `BOOST_AUTO_TEST_CASE(X)` (Rule B).
4. Remove `main()` (Rule C).
5. Wrap test cases in `BOOST_AUTO_TEST_SUITE(<ModuleName>) ... BOOST_AUTO_TEST_SUITE_END()`.

- [ ] **Step 13.1: Convert `timing_test.cpp`**

Module name: `Timing`. Build target: `timing_test`.

```bash
# After editing:
cd /home/pi/AuroreMkVII/build-rpi
cmake --build . --target timing_test && ctest -R timing_test -V
```

- [ ] **Step 13.2: Convert `ballistics_test.cpp`**

Module name: `BallisticSolver`. Build target: `ballistics_test`.

```bash
cmake --build . --target ballistics_test && ctest -R ballistics_test -V
```

- [ ] **Step 13.3: Convert `ballistics_stress_test.cpp`**

Module name: `BallisticSolverStress`. Build target: `ballistics_stress_test`.

```bash
cmake --build . --target ballistics_stress_test && ctest -R ballistics_stress_test -V
```

- [ ] **Step 13.4: Convert `gimbal_controller_test.cpp`**

Module name: `GimbalController`. Build target: `gimbal_controller_test`.

```bash
cmake --build . --target gimbal_controller_test && ctest -R gimbal_controller_test -V
```

- [ ] **Step 13.5: Convert `geometry_test.cpp`**

Module name: `Geometry`. Build target: `geometry_test`.

```bash
cmake --build . --target geometry_test && ctest -R geometry_test -V
```

- [ ] **Step 13.6: Commit**

```bash
cd /home/pi/AuroreMkVII
git add tests/unit/timing_test.cpp tests/unit/ballistics_test.cpp \
        tests/unit/ballistics_stress_test.cpp tests/unit/gimbal_controller_test.cpp \
        tests/unit/geometry_test.cpp
git commit -m "test(wave3): convert timing, ballistics, geometry tests to Boost.Test

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 14: Convert utility and network tests

**Files:**
- Modify: `tests/unit/firmware_updater_test.cpp`
- Modify: `tests/unit/raw10_test.cpp`
- Modify: `tests/unit/config_loader_test.cpp`
- Modify: `tests/unit/sequence_validation_test.cpp`
- Modify: `tests/unit/hud_socket_test.cpp`
- Modify: `tests/unit/hud_socket_stress_test.cpp`
- Modify: `tests/unit/telemetry_writer_test.cpp`
- Modify: `tests/unit/aurore_link_test.cpp`
- Modify: `tests/unit/test_emergency_inhibit.cpp`

Apply Rules A, B, C to each. Module names and build targets:

| File | `BOOST_TEST_MODULE` | Target |
|---|---|---|
| `firmware_updater_test.cpp` | `FirmwareUpdaterTest` | `firmware_updater_test` |
| `raw10_test.cpp` | `Raw10Test` | `raw10_test` |
| `config_loader_test.cpp` | `ConfigLoaderTest` | `config_loader_test` |
| `sequence_validation_test.cpp` | `SequenceValidationTest` | `sequence_validation_test` |
| `hud_socket_test.cpp` | `HudSocketTest` | `hud_socket_test` |
| `hud_socket_stress_test.cpp` | `HudSocketStressTest` | `hud_socket_stress_test` |
| `telemetry_writer_test.cpp` | `TelemetryWriterTest` | `telemetry_writer_test` |
| `aurore_link_test.cpp` | `AuroreLinkTest` | `aurore_link_test` |
| `test_emergency_inhibit.cpp` | `EmergencyInhibitTest` | `emergency_inhibit_test` |

For each file:

- [ ] **Step 14.1–14.9: Edit, build, run**

```bash
# Pattern for each (substitute <target>):
cmake --build . --target <target> 2>&1 | tail -3
ctest -R <target> -V 2>&1 | grep -E "PASS|FAIL|Skipped"
```

- [ ] **Step 14.10: Commit**

```bash
cd /home/pi/AuroreMkVII
git add tests/unit/firmware_updater_test.cpp tests/unit/raw10_test.cpp \
        tests/unit/config_loader_test.cpp tests/unit/sequence_validation_test.cpp \
        tests/unit/hud_socket_test.cpp tests/unit/hud_socket_stress_test.cpp \
        tests/unit/telemetry_writer_test.cpp tests/unit/aurore_link_test.cpp \
        tests/unit/test_emergency_inhibit.cpp
git commit -m "test(wave3): convert utility and network tests to Boost.Test

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 15: Convert remaining state machine and orchestration tests

**Files:**
- Modify: `tests/unit/test_state_machine_transitions.cpp`
- Modify: `tests/unit/state_machine_stress_test.cpp`
- Modify: `tests/unit/test_main_thread_orchestration.cpp`

Apply Rules A, B, C.

| File | `BOOST_TEST_MODULE` | Target |
|---|---|---|
| `test_state_machine_transitions.cpp` | `StateMachineTransitionsTest` | `state_machine_transitions_test` |
| `state_machine_stress_test.cpp` | `StateMachineStressTest` | `state_machine_stress_test` |
| `test_main_thread_orchestration.cpp` | `MainThreadOrchestrationTest` | `main_thread_orchestration_test` |

- [ ] **Step 15.1–15.3: Edit, build, run each**

```bash
cmake --build . --target <target> && ctest -R <target> -V
```

- [ ] **Step 15.4: Wave 3 checkpoint**

```bash
ctest -L tier0 --output-on-failure 2>&1 | tail -5
ctest -L tier1 --output-on-failure 2>&1 | tail -5
```

Expected: all tier0 and tier1 tests PASS.

- [ ] **Step 15.5: Commit**

```bash
cd /home/pi/AuroreMkVII
git add tests/unit/test_state_machine_transitions.cpp \
        tests/unit/state_machine_stress_test.cpp \
        tests/unit/test_main_thread_orchestration.cpp
git commit -m "test(wave3): convert state machine and orchestration tests to Boost.Test

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Wave 4 — Hardware Tests

### Task 16: Convert I2C hardware tests (FusionHAT, Interlock coupling)

**Files:**
- Modify: `tests/unit/fusion_hat_test.cpp`
- Modify: `tests/unit/test_coupling_control_actuation.cpp`
- Modify: `tests/hardware/gimbal_actuation_test.cpp`

These tests touch the FusionHAT on I2C bus 1. Apply Rules A, B, C, **and Rule E** (hardware fixture).

Hardware detection probe for FusionHAT:
```cpp
#include <cstdlib>

struct FusionHatHardwareGuard {
    FusionHatHardwareGuard() {
        aurore::FusionHat hat;
        if (!hat.probe()) {
            BOOST_TEST_MESSAGE(
                "HARDWARE ABSENT: FusionHAT not detected on I2C bus 1. "
                "Diagnose: i2cdetect -y 1 | grep -w '40'");
            std::_Exit(77);
        }
    }
};
BOOST_GLOBAL_FIXTURE(FusionHatHardwareGuard);
```

Place this **after** the `#include` block and **before** `BOOST_AUTO_TEST_SUITE(...)`.

- [ ] **Step 16.1: Convert `fusion_hat_test.cpp`**

Module: `FusionHatTest`. Add `FusionHatHardwareGuard` fixture. Build target: `fusion_hat_test`.

```bash
cmake --build . --target fusion_hat_test && ctest -R fusion_hat_test -V
```

Expected with hardware connected: PASS. Without hardware: `Skipped`.

- [ ] **Step 16.2: Convert `test_coupling_control_actuation.cpp`**

Module: `CouplingControlActuationTest`. Add `FusionHatHardwareGuard` (same fixture — tests require I2C). Build target: `coupling_control_actuation_test`.

```bash
cmake --build . --target coupling_control_actuation_test && ctest -R coupling_control_actuation_test -V
```

- [ ] **Step 16.3: Convert `gimbal_actuation_test.cpp`**

Module: `GimbalActuationTest`. Add `FusionHatHardwareGuard`. Build target: `gimbal_actuation_test`.

```bash
cmake --build . --target gimbal_actuation_test && ctest -R gimbal_actuation_test -V
```

- [ ] **Step 16.4: Commit**

```bash
cd /home/pi/AuroreMkVII
git add tests/unit/fusion_hat_test.cpp tests/unit/test_coupling_control_actuation.cpp \
        tests/hardware/gimbal_actuation_test.cpp
git commit -m "test(wave4): convert FusionHAT and gimbal tests to Boost.Test with hardware fixtures

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 17: Convert UART / laser tests

**Files:**
- Modify: `tests/unit/laser_rangefinder_test.cpp`
- Modify: `tests/unit/lrf_20_samples_test.cpp`
- Modify: `tests/hardware/boresight_convergence_test.cpp`
- Modify: `tests/integration/laser_validation_test.cpp`

Hardware detection probe for LRF on `/dev/ttyAMA0`:
```cpp
struct LaserHardwareGuard {
    LaserHardwareGuard() {
        aurore::LaserRangefinder lrf("/dev/ttyAMA0");
        if (!lrf.open()) {
            BOOST_TEST_MESSAGE(
                "HARDWARE ABSENT: /dev/ttyAMA0 not available. "
                "Verify: ls -l /dev/ttyAMA0");
            std::_Exit(77);
        }
    }
};
BOOST_GLOBAL_FIXTURE(LaserHardwareGuard);
```

Apply Rules A, B, C, E to each file.

- [ ] **Step 17.1–17.4: Convert, build, run each**

```bash
cmake --build . --target <target> && ctest -R <target> -V
```

- [ ] **Step 17.5: Commit**

```bash
cd /home/pi/AuroreMkVII
git add tests/unit/laser_rangefinder_test.cpp tests/unit/lrf_20_samples_test.cpp \
        tests/hardware/boresight_convergence_test.cpp \
        tests/integration/laser_validation_test.cpp
git commit -m "test(wave4): convert laser/UART tests to Boost.Test with hardware fixtures

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 18: Convert camera and vision hardware tests

**Files:**
- Modify: `tests/unit/camera_wrapper_test.cpp`
- Modify: `tests/unit/tracker_test.cpp`
- Modify: `tests/unit/detector_test.cpp`
- Modify: `tests/unit/test_vision_pipeline_latency.cpp`
- Modify: `tests/unit/test_gpu_acceleration.cpp`
- Modify: `tests/hardware/integration_check.cpp`

Hardware detection probe for CSI camera:
```cpp
#include <cstdlib>
#include "aurore/camera_wrapper.hpp"

struct CameraHardwareGuard {
    CameraHardwareGuard() {
        aurore::CameraWrapper cam;
        if (!cam.probe()) {
            BOOST_TEST_MESSAGE(
                "HARDWARE ABSENT: CSI camera not detected. "
                "Diagnose: rpicam-hello --list-cameras");
            std::_Exit(77);
        }
    }
};
BOOST_GLOBAL_FIXTURE(CameraHardwareGuard);
```

Apply Rules A, B, C, E to each file.

- [ ] **Step 18.1–18.6: Convert, build, run each**

```bash
cmake --build . --target <target> && ctest -R <target> -V
```

- [ ] **Step 18.7: Wave 4 checkpoint**

```bash
ctest -L hardware --output-on-failure 2>&1 | grep -E "Test #|PASS|FAIL|Skipped"
```

Expected: all tests either PASS (hardware connected) or Skipped (hardware absent). Zero FAIL results.

- [ ] **Step 18.8: Commit**

```bash
cd /home/pi/AuroreMkVII
git add tests/unit/camera_wrapper_test.cpp tests/unit/tracker_test.cpp \
        tests/unit/detector_test.cpp tests/unit/test_vision_pipeline_latency.cpp \
        tests/unit/test_gpu_acceleration.cpp tests/hardware/integration_check.cpp
git commit -m "test(wave4): convert camera and vision hardware tests to Boost.Test

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Wave 5 — Stress, Parameterised, and Full Suite

### Task 19: Convert stress and recovery tests

**Files:**
- Modify: `tests/unit/core_stress_test.cpp`
- Modify: `tests/unit/safety_stress_test.cpp`
- Modify: `tests/unit/test_safety_monitor_fault_codes.cpp`
- Modify: `tests/unit/concurrency_pathology_test.cpp`
- Modify: `tests/unit/temporal_consistency_test.cpp`
- Modify: `tests/unit/numeric_robustness_test.cpp`
- Modify: `tests/unit/hostile_input_test.cpp`
- Modify: `tests/unit/resource_exhaustion_test.cpp`
- Modify: `tests/unit/reset_recovery_test.cpp`
- Modify: `tests/unit/observability_test.cpp`
- Modify: `tests/unit/memory_resource_test.cpp`
- Modify: `tests/unit/fault_containment_test.cpp`
- Modify: `tests/unit/state_mode_integrity_test.cpp`

Apply Rules A, B, C. No hardware fixtures needed for these tests.

| File | `BOOST_TEST_MODULE` | Suite name |
|---|---|---|
| `core_stress_test.cpp` | `CoreStressTest` | `CoreStress` |
| `safety_stress_test.cpp` | `SafetyStressTest` | `SafetyStress` |
| `test_safety_monitor_fault_codes.cpp` | `SafetyMonitorFaultCodesTest` | `SafetyMonitorFaultCodes` |
| `concurrency_pathology_test.cpp` | `ConcurrencyPathologyTest` | `ConcurrencyPathology` |
| `temporal_consistency_test.cpp` | `TemporalConsistencyTest` | `TemporalConsistency` |
| `numeric_robustness_test.cpp` | `NumericRobustnessTest` | `NumericRobustness` |
| `hostile_input_test.cpp` | `HostileInputTest` | `HostileInput` |
| `resource_exhaustion_test.cpp` | `ResourceExhaustionTest` | `ResourceExhaustion` |
| `reset_recovery_test.cpp` | `ResetRecoveryTest` | `ResetRecovery` |
| `observability_test.cpp` | `ObservabilityTest` | `Observability` |
| `memory_resource_test.cpp` | `MemoryResourceTest` | `MemoryResource` |
| `fault_containment_test.cpp` | `FaultContainmentTest` | `FaultContainment` |
| `state_mode_integrity_test.cpp` | `StateModeIntegrityTest` | `StateModeIntegrity` |

- [ ] **Step 19.1–19.13: Convert, build, run each**

```bash
cmake --build . --target <target> && ctest -R <target> -V
```

- [ ] **Step 19.14: Commit**

```bash
cd /home/pi/AuroreMkVII
git add tests/unit/core_stress_test.cpp tests/unit/safety_stress_test.cpp \
        tests/unit/test_safety_monitor_fault_codes.cpp \
        tests/unit/concurrency_pathology_test.cpp \
        tests/unit/temporal_consistency_test.cpp \
        tests/unit/numeric_robustness_test.cpp \
        tests/unit/hostile_input_test.cpp \
        tests/unit/resource_exhaustion_test.cpp \
        tests/unit/reset_recovery_test.cpp \
        tests/unit/observability_test.cpp \
        tests/unit/memory_resource_test.cpp \
        tests/unit/fault_containment_test.cpp \
        tests/unit/state_mode_integrity_test.cpp
git commit -m "test(wave5): convert stress and recovery tests to Boost.Test

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 20: Convert parameterised matrix tests

**Files:**
- Modify: `tests/unit/safety/parameterized/estop_matrix.cpp`
- Modify: `tests/unit/safety/parameterized/watchdog_timeout_matrix.cpp`

These use manual loop-over-array patterns. Convert to `BOOST_DATA_TEST_CASE`.

- [ ] **Step 20.1: Read each file to extract the data arrays and loop structure**

The general pattern in these files is:
```cpp
void run_estop_matrix() {
    static const FaultCode faults[] = { ... };
    for (auto fault : faults) {
        SafetyMonitor monitor(cfg);
        monitor.inject_fault(fault);
        ASSERT_TRUE(monitor.is_emergency_active());
    }
}
```

- [ ] **Step 20.2: Convert `estop_matrix.cpp`**

Replace the manual loop with:
```cpp
#define BOOST_TEST_MODULE EstopMatrixTest
#include <boost/test/unit_test.hpp>
#include <boost/test/data/test_case.hpp>
namespace bdata = boost::unit_test::data;

#include "aurore/safety_monitor.hpp"

// Populate with the actual fault codes from the original array
static const aurore::FaultCode kEstopFaults[] = {
    // ... copy the original array elements verbatim ...
};

BOOST_AUTO_TEST_SUITE(EstopMatrix)

BOOST_DATA_TEST_CASE(each_fault_triggers_emergency,
                     bdata::make(kEstopFaults), fault_code)
{
    aurore::SafetyMonitorConfig cfg;
    cfg.enable_watchdog = false;
    aurore::SafetyMonitor monitor(cfg);
    monitor.inject_fault(fault_code);
    BOOST_CHECK(monitor.is_emergency_active());
}

BOOST_AUTO_TEST_SUITE_END()
```

```bash
cmake --build . --target estop_matrix_test && ctest -R estop_matrix_test -V
```

- [ ] **Step 20.3: Convert `watchdog_timeout_matrix.cpp`**

Apply the same `BOOST_DATA_TEST_CASE` pattern. Preserve all timing values from the original array verbatim.

```bash
cmake --build . --target watchdog_timeout_matrix_test && ctest -R watchdog_timeout_matrix_test -V
```

- [ ] **Step 20.4: Commit**

```bash
cd /home/pi/AuroreMkVII
git add tests/unit/safety/parameterized/estop_matrix.cpp \
        tests/unit/safety/parameterized/watchdog_timeout_matrix.cpp
git commit -m "test(wave5): convert parameterised matrix tests to BOOST_DATA_TEST_CASE

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 21: Convert full suite, security, detection, and timing tests

**Files:**
- Modify: `tests/unit/safety/aurore_full_test_suite.cpp`
- Modify: `tests/unit/test_security_stress.cpp`
- Modify: `tests/unit/test_gimbal_command_rate.cpp`
- Modify: `tests/unit/test_detection_rate.cpp`
- Modify: `tests/rt_bench.cpp`
- Modify: `tests/timing/wcet_measurement.cpp`
- Modify: `tests/timing/jitter_analysis.cpp`
- Modify: `tests/integration/safety_fault_injection_test.cpp`
- Modify: `tests/integration/thermal_dma_health_test.cpp`

> **Note on `aurore_full_test_suite.cpp`:** This file is the 237-test unified suite. After conversion, it will contain multiple `BOOST_AUTO_TEST_SUITE` blocks. Do NOT collapse them into a single suite — preserve the logical grouping structure that already exists in the file.

> **Note on `aurore_timing_tests`:** `wcet_measurement.cpp` and `jitter_analysis.cpp` are compiled into a single binary. Each file must define a `BOOST_AUTO_TEST_SUITE` block but **neither** should define `BOOST_TEST_MODULE` — only one file in a multi-source binary may define `BOOST_TEST_MODULE`. Add a separate `tests/timing/timing_main.cpp` that defines only:
> ```cpp
> #define BOOST_TEST_MODULE AuroreTimingTests
> #include <boost/test/unit_test.hpp>
> ```
> Then add `tests/timing/timing_main.cpp` to the `aurore_timing_tests` SOURCES in CMakeLists.txt.

Apply Rules A, B, C to each file. For `test_security_stress.cpp`, `test_detection_rate.cpp`, and `thermal_dma_health_test.cpp` — these may require hardware fixtures (check if they open camera or I2C); if so, apply Rule E.

- [ ] **Step 21.1–21.9: Convert, build, run each target**

```bash
cmake --build . --target <target> && ctest -R <target> -V
```

- [ ] **Step 21.10: Commit**

```bash
cd /home/pi/AuroreMkVII
git add tests/unit/safety/aurore_full_test_suite.cpp \
        tests/unit/test_security_stress.cpp \
        tests/unit/test_gimbal_command_rate.cpp \
        tests/unit/test_detection_rate.cpp \
        tests/rt_bench.cpp \
        tests/timing/timing_main.cpp \
        tests/timing/wcet_measurement.cpp \
        tests/timing/jitter_analysis.cpp \
        tests/integration/safety_fault_injection_test.cpp \
        tests/integration/thermal_dma_health_test.cpp \
        CMakeLists.txt   # timing_main.cpp added to SOURCES
git commit -m "test(wave5): convert full suite, timing, and integration tests to Boost.Test

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 22: Update `CLAUDE.md`

**Files:**
- Modify: `CLAUDE.md`

- [ ] **Step 22.1: Locate the existing Testing section**

```bash
grep -n "## Testing\|ctest\|ring_buffer_test\|timing_test" /home/pi/AuroreMkVII/CLAUDE.md | head -20
```

- [ ] **Step 22.2: Replace the Testing section**

Find the `## Testing` section in `CLAUDE.md` and replace its content with:

```markdown
## Testing Workflow

### Run tests
```bash
cd build-rpi && ctest --output-on-failure           # full suite
cd build-rpi && ctest -L safety --output-on-failure  # safety-critical only
cd build-rpi && ctest -L tier0  --output-on-failure  # fast unit tests only
cd build-rpi && ctest -L hardware --output-on-failure # HIL tests (requires rig)
```

### Add a new test
In `CMakeLists.txt`, inside `if(AURORE_ENABLE_TESTS)`, use `aurore_add_test()`:

```cmake
aurore_add_test(
  NAME    my_feature_test
  SOURCES tests/unit/my_feature_test.cpp
          src/module/my_feature.cpp
  LIBS    rt OpenSSL::Crypto
  LABELS  "tier1;safety"
  TIMEOUT 60
  # HARDWARE          # uncomment if test opens real hardware
  # RESOURCE_LOCK I2C_BUS_1  # required for hardware tests
)
```

### Test file naming convention
- `tests/unit/<module>_test.cpp` — pure logic, no hardware
- `tests/integration/<module>_test.cpp` — requires connected subsystem
- `tests/hardware/<module>_test.cpp` — requires full hardware rig

### Rules (non-negotiable)
1. Every new feature or bugfix requires a corresponding `BOOST_AUTO_TEST_CASE`.
2. **NO MOCKS** — no test doubles, no simulated hardware, no fakes.
   Hardware absent → test calls `std::_Exit(77)` after logging diagnostic. Never silently passes.
3. Hardware tests use `BOOST_GLOBAL_FIXTURE` to detect the peripheral at suite entry.
   Diagnostic format: `"HARDWARE ABSENT: <peripheral>. Diagnose: <shell command>"`.
4. **Resource locks are mandatory for hardware tests**: `I2C_BUS_1`, `uart_ttyAMA0`,
   `CSI_CAMERA_0`, `GPIO_BANK_0`. Prevents CTest `-j` from corrupting peripherals.
5. Exit code 77 = hardware absent (CTest: Skipped). Any other non-zero = logic failure (CTest: Failed).
6. Use `BOOST_AUTO_TEST_CASE` / `BOOST_CHECK` / `BOOST_REQUIRE`.
   Hand-rolled `TEST()` / `RUN_TEST()` / `ASSERT_*` macros are **forbidden** in new code.
```

- [ ] **Step 22.3: Commit**

```bash
cd /home/pi/AuroreMkVII
git add CLAUDE.md
git commit -m "docs: update CLAUDE.md with Boost.Test workflow and testing rules

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 23: Final checkpoint, full suite run, and push

- [ ] **Step 23.1: Full rebuild from clean**

```bash
cd /home/pi/AuroreMkVII/build-rpi
cmake --build . --clean-first -j$(nproc) 2>&1 | tail -10
```

Expected: zero build errors, zero warnings in test files.

- [ ] **Step 23.2: Run full test suite**

```bash
ctest --output-on-failure 2>&1 | tee /tmp/ctest_final.txt
tail -20 /tmp/ctest_final.txt
```

Expected:
- All unit tests (tier0, tier1): **PASS**
- Hardware tests without rig: **Skipped** (exit 77)
- Hardware tests with rig connected: **PASS**
- Zero **Failed** results

- [ ] **Step 23.3: Confirm no hand-rolled macros remain in test files**

```bash
grep -r "RUN_TEST\|#define TEST\|ASSERT_TRUE\|ASSERT_EQ\|ASSERT_FALSE" \
     /home/pi/AuroreMkVII/tests/ --include="*.cpp" | grep -v "//.*RUN_TEST"
```

Expected: zero matches. Any matches indicate a file not yet converted.

- [ ] **Step 23.4: Confirm no AURORE_LAPTOP_BUILD references remain**

```bash
grep -r "AURORE_LAPTOP_BUILD" /home/pi/AuroreMkVII/tests/ --include="*.cpp"
```

Expected: zero matches.

- [ ] **Step 23.5: Push to remote**

```bash
cd /home/pi/AuroreMkVII
git log --oneline origin/main..HEAD
git push origin main
```

Expected: all Wave 1–5 commits pushed cleanly.

---

## Appendix: Resource Lock Names

| Peripheral | Lock name used in `aurore_add_test(RESOURCE_LOCK ...)` |
|---|---|
| FusionHAT / IMU on I2C bus 1 | `I2C_BUS_1` |
| Laser rangefinder on `/dev/ttyAMA0` | `uart_ttyAMA0` |
| CSI camera on `/dev/video0` | `CSI_CAMERA_0` |
| GPIO bank 0 (interlock relay) | `GPIO_BANK_0` |

## Appendix: Boost.Test Assertion Mapping

| Old | New | Notes |
|---|---|---|
| `ASSERT_TRUE(x)` | `BOOST_CHECK(x)` | non-fatal |
| `ASSERT_FALSE(x)` | `BOOST_CHECK(!x)` | non-fatal |
| `ASSERT_EQ(a, b)` | `BOOST_CHECK_EQUAL(a, b)` | non-fatal |
| `assert(x)` | `BOOST_REQUIRE(x)` | fatal — aborts test case |
| `CHECK(cond, msg)` | `BOOST_CHECK_MESSAGE(cond, msg)` | non-fatal with message |
| precondition `ASSERT_TRUE` | `BOOST_REQUIRE(x)` | use REQUIRE for preconditions |
| floating-point equality | `BOOST_CHECK_CLOSE(a, b, tol)` | tolerance in % |
