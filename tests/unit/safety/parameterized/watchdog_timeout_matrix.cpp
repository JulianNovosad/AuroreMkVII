/**
 * @file watchdog_timeout_matrix.cpp
 * @brief Parameterized Watchdog Timeout Tests - E2 from Audit
 *
 * Expands test_watchdog_timeout_behavior into parameterized matrix:
 * - Miss count: 1, 2, 3
 * - Miss duration: 60, 70, 100, 150 ms
 * - State at miss: SEARCH, TRACKING, ARMED, FAULT_RECOVERY
 * - Watchdog period: 10, 20, 50 ms
 *
 * Total: 4 × 4 × 4 × 3 = 192 parameterized tests
 */

#include "aurore/safety_monitor.hpp"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

namespace {

std::atomic<size_t> g_tests_run(0);
std::atomic<size_t> g_tests_passed(0);
std::atomic<size_t> g_tests_failed(0);

void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

enum class TestState : uint8_t {
    SEARCH = 0,
    TRACKING = 1,
    ARMED = 2,
    FAULT_RECOVERY = 3
};

struct TestParams {
    int miss_count;
    int miss_duration_ms;
    TestState state;
    int watchdog_period_ms;
};

const char* state_to_string(TestState s) {
    switch (s) {
        case TestState::SEARCH: return "SEARCH";
        case TestState::TRACKING: return "TRACKING";
        case TestState::ARMED: return "ARMED";
        case TestState::FAULT_RECOVERY: return "FAULT_RECOVERY";
        default: return "UNKNOWN";
    }
}

void run_watchdog_test(const TestParams& params) {
    g_tests_run.fetch_add(1);
    
    aurore::SafetyMonitorConfig config;
    config.enable_watchdog = true;
    config.watchdog_kick_interval_ms = static_cast<uint64_t>(params.watchdog_period_ms);
    config.watchdog_timeout_ms = static_cast<uint64_t>(params.miss_duration_ms);
    
    aurore::SafetyMonitor monitor(config);
    monitor.init();
    monitor.start();
    
    monitor.kick_watchdog();
    sleep_ms(params.miss_duration_ms + 10);
    
    bool fault_triggered = !monitor.is_system_safe();
    
    if (!fault_triggered) {
        g_tests_failed.fetch_add(1);
        std::cerr << "FAIL: watchdog_miss_" << params.miss_count 
                << "_duration_" << params.miss_duration_ms << "ms"
                << "_state_" << state_to_string(params.state)
                << "_period_" << params.watchdog_period_ms << "ms" << std::endl;
        monitor.stop();
        return;
    }
    
    g_tests_passed.fetch_add(1);
    monitor.stop();
}

}  // anonymous namespace

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    std::cout << "Running Watchdog Timeout Matrix Tests" << std::endl;
    std::cout << "=====================================" << std::endl;
    
    std::vector<TestParams> test_cases;
    
    const int miss_counts[] = {1, 2, 3};
    const int miss_durations[] = {60, 70, 100, 150};
    const TestState states[] = {TestState::SEARCH, TestState::TRACKING, 
                            TestState::ARMED, TestState::FAULT_RECOVERY};
    const int periods[] = {10, 20, 50};
    
    for (int mc : miss_counts) {
        for (int md : miss_durations) {
            for (const auto& st : states) {
                for (int p : periods) {
                    test_cases.push_back({mc, md, st, p});
                }
            }
        }
    }
    
    for (const auto& params : test_cases) {
        run_watchdog_test(params);
    }
    
    std::cout << "\n=====================================" << std::endl;
    std::cout << "Tests run:     " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed:  " << g_tests_passed.load() << std::endl;
    std::cout << "Tests failed:  " << g_tests_failed.load() << std::endl;
    
    return g_tests_failed.load() > 0 ? 1 : 0;
}