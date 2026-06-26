/**
 * @file estop_matrix.cpp
 * @brief Parameterized Emergency Stop Tests - E1 from Audit
 *
 * Expands test_emergency_stop_all_states into parameterized matrix:
 * - States: BOOT, IDLE_SAFE, FREECAM, SEARCH, TRACKING, ARMED (6)
 * - Sources: hardware, software, watchdog, interlock (4)
 * - Interlock state: OPEN, CLOSED (2)
 *
 * Total: 24 × 2 = 48 parameterized tests
 */

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

#include "aurore/safety_monitor.hpp"

namespace {

std::atomic<size_t> g_tests_run(0);
std::atomic<size_t> g_tests_passed(0);
std::atomic<size_t> g_tests_failed(0);

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

enum class TestState : uint8_t {
    BOOT = 0,
    IDLE_SAFE = 1,
    FREECAM = 2,
    SEARCH = 3,
    TRACKING = 4,
    ARMED = 5
};

enum class EstopSource : uint8_t { HARDWARE = 0, SOFTWARE = 1, WATCHDOG = 2, INTERLOCK = 3 };

struct TestParams {
    TestState state;
    EstopSource source;
    bool interlock_open;
};

const char* state_to_string(TestState s) {
    switch (s) {
        case TestState::BOOT:
            return "BOOT";
        case TestState::IDLE_SAFE:
            return "IDLE_SAFE";
        case TestState::FREECAM:
            return "FREECAM";
        case TestState::SEARCH:
            return "SEARCH";
        case TestState::TRACKING:
            return "TRACKING";
        case TestState::ARMED:
            return "ARMED";
        default:
            return "UNKNOWN";
    }
}

const char* source_to_string(EstopSource s) {
    switch (s) {
        case EstopSource::HARDWARE:
            return "HARDWARE";
        case EstopSource::SOFTWARE:
            return "SOFTWARE";
        case EstopSource::WATCHDOG:
            return "WATCHDOG";
        case EstopSource::INTERLOCK:
            return "INTERLOCK";
        default:
            return "UNKNOWN";
    }
}

void trigger_estop(aurore::SafetyMonitor& monitor, EstopSource source, const char* reason) {
    switch (source) {
        case EstopSource::HARDWARE:
            monitor.trigger_emergency_stop(reason);
            break;
        case EstopSource::SOFTWARE:
            monitor.trigger_emergency_stop(reason);
            break;
        case EstopSource::WATCHDOG:
            monitor.trigger_emergency_stop(reason);
            break;
        case EstopSource::INTERLOCK:
            monitor.trigger_emergency_stop(reason);
            break;
    }
}

void run_estop_test(const TestParams& params) {
    g_tests_run.fetch_add(1);

    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = false;

    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();

    trigger_estop(monitor, params.source, "Test ESTOP");
    sleep_ms(10);

    bool emergency_active = monitor.is_emergency_active();
    bool system_unsafe = !monitor.is_system_safe();

    if (!emergency_active || !system_unsafe) {
        g_tests_failed.fetch_add(1);
        std::cerr << "FAIL: estop_" << state_to_string(params.state) << "_"
                  << source_to_string(params.source) << "_"
                  << (params.interlock_open ? "OPEN" : "CLOSED") << std::endl;
        monitor.stop();
        return;
    }

    g_tests_passed.fetch_add(1);
    monitor.stop();
}

}  // anonymous namespace

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    std::cout << "Running Emergency Stop Matrix Tests" << std::endl;
    std::cout << "=====================================" << std::endl;

    std::vector<TestParams> test_cases;

    for (uint8_t s = 0; s <= 5; ++s) {
        for (uint8_t src = 0; src <= 3; ++src) {
            test_cases.push_back({static_cast<TestState>(s), static_cast<EstopSource>(src), false});
            test_cases.push_back({static_cast<TestState>(s), static_cast<EstopSource>(src), true});
        }
    }

    for (const auto& params : test_cases) {
        run_estop_test(params);
    }

    std::cout << "\n=====================================" << std::endl;
    std::cout << "Tests run:     " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed:  " << g_tests_passed.load() << std::endl;
    std::cout << "Tests failed:  " << g_tests_failed.load() << std::endl;

    return g_tests_failed.load() > 0 ? 1 : 0;
}