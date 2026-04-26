# Boost.Test Migration Design — AuroreMkVII

**Date:** 2026-04-26  
**Status:** Approved  
**Author:** ADA (Aurore Development Agent)

---

## 1. Objective

Replace the hand-rolled `TEST()`/`RUN_TEST()`/`ASSERT_*` macro harness used across all 61 test files with Boost.Test 1.83. Simultaneously eliminate the ~1,400-line CMakeLists.txt boilerplate by introducing a single `aurore_add_test()` CMake function. No mocks, no fakes, no test doubles of any kind are introduced at any step.

---

## 2. Constraints (Non-Negotiable)

1. **No mocks.** Every test that touches hardware must connect to real hardware. If hardware is absent, the test must fail with an explicit diagnostic message — never produce a passing result via any substitute.
2. **Boost.Test 1.83 only.** Already installed at `/usr/include/boost/test/`. No FetchContent, no network dependencies.
3. **Target: Raspberry Pi 5 (aarch64).** All tests run on-device. CI runs on the Pi.
4. **MISRA C++:2023 compliance** must be preserved in all converted test code.

---

## 3. CMake Architecture

### 3.1 Helper Module

A new file `cmake/AuroreTestHelpers.cmake` defines `aurore_add_test()` and is `include()`d once near the top of the root `CMakeLists.txt`.

```cmake
find_package(Boost 1.83 REQUIRED COMPONENTS unit_test_framework)

# ---------------------------------------------------------------------------
# aurore_add_test(
#   NAME             <target-name>
#   SOURCES          <file.cpp> [<file.cpp>...]
#   [LIBS            <lib> [<lib>...]]
#   [LABELS          "<label>[;<label>...]"]
#   [TIMEOUT         <seconds>]          # default: 60
#   [RESOURCE_LOCK   <lock-name> [<lock-name>...]]
#   [HARDWARE]       # marks test as hardware-requiring; absent hardware → SKIP (exit 77)
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

    # Boost::unit_test_framework (CMake imported target) handles static vs.
    # dynamic link mode automatically — do NOT add BOOST_TEST_DYN_LINK manually.
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
    # HARDWARE tests exit with code 77 on absent hardware; CTest marks them SKIP,
    # not FAIL — distinguishing "rig not attached" from a logic regression.
    if(A_HARDWARE)      list(APPEND _props SKIP_RETURN_CODE 77)               endif()
    set_tests_properties(${A_NAME} PROPERTIES ${_props})
endfunction()
```

### 3.2 Exit Code Contract

| Exit code | Meaning | CTest result |
|---|---|---|
| `0` | All tests passed | PASS |
| `77` | Hardware absent (fixture called `std::_Exit(77)`) | SKIP (via `SKIP_RETURN_CODE 77`) |
| any other non-zero | Test assertion failed (Boost.Test default) | FAIL |

Hardware-absent failures (`exit(77)`) and logic regressions (non-zero ≠ 77) are distinguishable in CI without log parsing. CTest's `--output-on-failure` still prints the diagnostic message so the operator knows which peripheral is missing.

### 3.3 Resource Lock Convention

Every hardware test **must** declare a `RESOURCE_LOCK` for the peripheral it uses. Defined names:

| Peripheral | Lock name |
|---|---|
| I2C bus 1 (FusionHAT, IMU) | `I2C_BUS_1` |
| UART `/dev/ttyAMA0` (LRF) | `uart_ttyAMA0` |
| CSI camera `/dev/video0` | `CSI_CAMERA_0` |
| GPIO bank 0 (interlock) | `GPIO_BANK_0` |

This ensures CTest never runs two tests that claim the same peripheral simultaneously, even if `-j` is passed.

### 3.4 Before/After CMakeLists Size

| Metric | Before | After (target) |
|---|---|---|
| `add_executable` calls | 58 | 0 (replaced by `aurore_add_test`) |
| `add_test` calls | 58 | 0 (inside helper) |
| `set_tests_properties` calls | 58 | 0 (inside helper) |
| `target_include_directories` calls | 58 | 0 (inside helper) |
| Estimated line count | ~1,400 | ~450 |

---

## 4. Boost.Test Code Patterns

### 4.1 Unit Test (Pure Logic — No Hardware)

```cpp
#define BOOST_TEST_MODULE <ModuleName>Test
#include <boost/test/unit_test.hpp>

#include "aurore/<module>.hpp"

BOOST_AUTO_TEST_SUITE(<ModuleName>)

BOOST_AUTO_TEST_CASE(construction) {
    // arrange
    aurore::Foo foo;
    // assert
    BOOST_REQUIRE(foo.is_valid());
    BOOST_CHECK_EQUAL(foo.count(), 0U);
}

BOOST_AUTO_TEST_SUITE_END()
```

Rules:
- One `BOOST_TEST_MODULE` per `.cpp` file (defines `main()`).
- `BOOST_REQUIRE` for preconditions — test aborts on failure.
- `BOOST_CHECK` for non-fatal assertions — test continues on failure.
- No `std::cout` pass/fail printing; Boost.Test handles output.

### 4.2 Hardware Test — Per-Suite Global Fixture

Used when the entire suite requires a peripheral. The fixture constructor detects the hardware; if absent it prints a diagnostic and calls `std::_Exit(77)`, which CTest maps to SKIP (not FAIL) via `SKIP_RETURN_CODE 77`.

```cpp
#define BOOST_TEST_MODULE FusionHatTest
#include <boost/test/unit_test.hpp>
#include <cstdlib>
#include "aurore/fusion_hat.hpp"

struct FusionHatHardwareGuard {
    FusionHatHardwareGuard() {
        aurore::FusionHat hat;
        if (!hat.probe()) {
            BOOST_TEST_MESSAGE(
                "HARDWARE ABSENT: FusionHAT not detected on I2C bus 1. "
                "Diagnose: i2cdetect -y 1 | grep -w '40'");
            std::_Exit(77);  // CTest SKIP_RETURN_CODE — not a logic failure
        }
    }
};
BOOST_GLOBAL_FIXTURE(FusionHatHardwareGuard);

BOOST_AUTO_TEST_SUITE(FusionHat)

BOOST_AUTO_TEST_CASE(init_and_home) {
    aurore::FusionHat hat;
    BOOST_REQUIRE(hat.init());
    BOOST_CHECK(hat.home());
}

BOOST_AUTO_TEST_SUITE_END()
```

### 4.3 Hardware Test — Per-Case Detection

Used when only some cases in a suite touch hardware. The case itself detects the hardware and exits 77 on absence.

```cpp
BOOST_AUTO_TEST_CASE(uart_framing) {
    aurore::LaserRangefinder lrf("/dev/ttyAMA0");
    if (!lrf.open()) {
        BOOST_TEST_MESSAGE(
            "HARDWARE ABSENT: /dev/ttyAMA0 not available. "
            "Verify: ls -l /dev/ttyAMA0");
        std::_Exit(77);
    }
    // test body...
}
```

### 4.4 Parameterised / Matrix Tests

Replaces manual loop-over-array patterns. Uses `boost::unit_test::data`.

```cpp
#include <boost/test/data/test_case.hpp>
namespace bdata = boost::unit_test::data;

static const aurore::FaultCode kEstopFaults[] = {
    aurore::FaultCode::kVisionTimeout,
    aurore::FaultCode::kActuationTimeout,
    aurore::FaultCode::kWatchdogExpired,
    aurore::FaultCode::kSensorLoss,
};

BOOST_DATA_TEST_CASE(estop_triggers_emergency,
                     bdata::make(kEstopFaults), fault_code)
{
    aurore::SafetyMonitor monitor(default_cfg());
    monitor.inject_fault(fault_code);
    BOOST_CHECK(monitor.is_emergency_active());
}
```

---

## 5. Migration Waves

### Wave 1 — CMake Refactor (no test code changes)

**Goal:** `cmake/AuroreTestHelpers.cmake` exists; all existing test targets re-registered using `aurore_add_test()`; build passes; `ctest` output identical to before.

**Sub-tasks:**
1. Write `cmake/AuroreTestHelpers.cmake`.
2. Add `include(cmake/AuroreTestHelpers.cmake)` to root `CMakeLists.txt`.
3. Replace all 58 manual test registrations with `aurore_add_test()` calls.
4. Scan `src/` for orphaned `.cpp` files not referenced in CMakeLists.txt and delete them (MkVI migration residue).
5. Verify: `cmake .. && cmake --build . -j$(nproc) && ctest --output-on-failure`.

**Checkpoint:** `ctest` passes with same test count as before.

### Wave 2 — Safety-Critical Tests (4 tests)

**Target files:**
- `tests/unit/ring_buffer_test.cpp`
- `tests/unit/safety_monitor_test.cpp`
- `tests/unit/state_machine_test.cpp`
- `tests/unit/interlock_controller_test.cpp`

**Checkpoint (mandatory):** After Wave 2, run:
```bash
ctest -L safety --output-on-failure
```
All 4 tests must appear in the filtered run and pass. This verifies label propagation through `aurore_add_test()`.

### Wave 3 — Pure Logic Unit Tests (no hardware)

Targets: timing, ballistics, ballistics_stress, gimbal_controller, geometry, firmware_updater, raw10, config_loader, sequence_validation, state_machine_transitions, state_machine_stress, main_thread_orchestration, aurore_link, emergency_inhibit, hud_socket, hud_socket_stress, telemetry_writer.

**Checkpoint:** `ctest -L tier0 -L tier1 --output-on-failure` — all pass.

### Wave 4 — Hardware / Integration Tests

Targets: fusion_hat_test, laser_rangefinder_test, lrf_20_samples_test, laser_validation_test, camera_wrapper_test, tracker_test, detector_test, gimbal_actuation_test, boresight_convergence_test, vision_pipeline_latency_test, integration_check.

All must declare `RESOURCE_LOCK`. Hardware fixtures added.

**Checkpoint:** Run with hardware connected; all pass or exit 200 (hardware absent is acceptable in CI without rig).

### Wave 5 — Stress, Parameterised, and Full Suite

Targets: core_stress_test, safety_stress_test, safety_monitor_fault_codes_test, concurrency_pathology_test, temporal_consistency_test, numeric_robustness_test, hostile_input_test, resource_exhaustion_test, reset_recovery_test, observability_test, memory_resource_test, fault_containment_test, state_mode_integrity_test, estop_matrix_test, watchdog_timeout_matrix_test, aurore_full_test_suite, security_stress_test, coupling_control_actuation_test, gimbal_command_rate_test, detection_rate_test, rt_bench, aurore_timing_tests, safety_fault_injection_test, thermal_dma_health_test, gpu_acceleration_test.

Parameterised tests converted to `BOOST_DATA_TEST_CASE`.

**Checkpoint:** `ctest --output-on-failure` — full suite passes.

---

## 6. Orphaned File Scan (Wave 1 Sub-task)

During Wave 1, check every `.cpp` in `src/` against the set of sources referenced in CMakeLists.txt. Files unreferenced by any `add_executable` or `add_library` call are candidates for deletion. Each candidate must be confirmed as MkVI residue (not a future stub) before removal. Keep stubs that are explicitly commented-out TODOs in the spec.

---

## 7. CLAUDE.md Delta

The following section replaces the existing `## Testing` bullet points in `CLAUDE.md`:

```markdown
## Testing Workflow

### Run tests
cd build-rpi && ctest --output-on-failure           # full suite
cd build-rpi && ctest -L safety --output-on-failure  # safety-critical only
cd build-rpi && ctest -L tier0  --output-on-failure  # fast unit tests only
cd build-rpi && ctest -L hardware --output-on-failure # HIL tests (requires rig)

### Add a new test
In CMakeLists.txt use aurore_add_test():

  aurore_add_test(
    NAME    my_feature_test
    SOURCES tests/unit/my_feature_test.cpp
            src/module/my_feature.cpp
    LIBS    rt OpenSSL::Crypto
    LABELS  "tier1;safety"
    TIMEOUT 60
    RESOURCE_LOCK I2C_BUS_1   # omit if no hardware
  )

### Test file naming convention
  tests/unit/<module>_test.cpp          pure logic, no hardware
  tests/integration/<module>_test.cpp   requires connected subsystem
  tests/hardware/<module>_test.cpp      requires full hardware rig

### Rules
1. Every new feature or bugfix requires a corresponding Boost.Test case.
2. NO MOCKS — no test doubles, no simulated hardware, no fakes.
   Hardware absent → test fails with diagnostic. Never silently passes.
3. Hardware tests use BOOST_GLOBAL_FIXTURE or BOOST_TEST_REQUIRE to
   detect the peripheral and fail with i2cdetect/dmesg guidance.
4. Resource locks are mandatory for hardware tests (I2C_BUS_1,
   uart_ttyAMA0, CSI_CAMERA_0, GPIO_BANK_0). Prevents parallel corruption.
5. Exit code 77 = hardware absent (CTest marks SKIP, not FAIL).
   Any other non-zero = logic regression (CTest marks FAIL).
   CI pipelines must distinguish these to avoid false "hardware broken" alerts.
6. Use BOOST_AUTO_TEST_CASE / BOOST_CHECK / BOOST_REQUIRE.
   Hand-rolled TEST()/RUN_TEST() macros are forbidden in new code.
```

---

## 8. Out of Scope

- Introducing any mock or fake objects.
- Cross-compilation from a host machine (all compilation and test execution happens on the Pi 5).
- Adding new test coverage beyond what the existing files already test (that is a separate effort).
- Changing test logic — conversion is structural only; existing assertions are preserved.
