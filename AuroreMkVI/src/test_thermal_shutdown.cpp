#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <cmath>

#include "thermal_shutdown.h"

using namespace aurore::thermal;

bool test_singleton() {
    std::cout << "\n=== Test: Singleton Instance ===" << std::endl;

    ThermalShutdownController& instance1 = ThermalShutdownController::instance();
    ThermalShutdownController& instance2 = ThermalShutdownController::instance();

    bool same_instance = (&instance1 == &instance2);
    std::cout << "  Same instance: " << (same_instance ? "PASS" : "FAIL") << std::endl;

    return same_instance;
}

bool test_default_config() {
    std::cout << "\n=== Test: Default Configuration ===" << std::endl;

    ThermalShutdownController::instance().configure_defaults();
    ThermalStatus status = ThermalShutdownController::instance().get_status();

    bool normal = (status.state == ThermalState::NORMAL);
    bool no_shutdown = !status.shutdown_triggered;
    bool temps_zero = (status.cpu_temperature == 0.0f && status.tpu_temperature == 0.0f);

    std::cout << "  Initial state: " << ThermalShutdownController::instance().get_state_string() << std::endl;
    std::cout << "  Shutdown triggered: " << (status.shutdown_triggered ? "YES" : "NO") << std::endl;
    std::cout << "  Result: " << ((normal && no_shutdown && temps_zero) ? "PASS" : "FAIL") << std::endl;

    return normal && no_shutdown && temps_zero;
}

bool test_temperature_update() {
    std::cout << "\n=== Test: Temperature Update ===" << std::endl;

    ThermalShutdownController::instance().reset();
    ThermalShutdownController::instance().configure_defaults();

    ThermalShutdownController::instance().update_temperatures(45.0f, 50.0f);

    bool correct_cpu = (ThermalShutdownController::instance().get_cpu_temperature() == 45.0f);
    bool correct_tpu = (ThermalShutdownController::instance().get_tpu_temperature() == 50.0f);
    bool normal_state = (ThermalShutdownController::instance().get_state() == ThermalState::NORMAL);

    std::cout << "  CPU temp: " << ThermalShutdownController::instance().get_cpu_temperature() << "°C" << std::endl;
    std::cout << "  TPU temp: " << ThermalShutdownController::instance().get_tpu_temperature() << "°C" << std::endl;
    std::cout << "  State: " << ThermalShutdownController::instance().get_state_string() << std::endl;
    std::cout << "  Result: " << ((correct_cpu && correct_tpu && normal_state) ? "PASS" : "FAIL") << std::endl;

    return correct_cpu && correct_tpu && normal_state;
}

bool test_warning_state() {
    std::cout << "\n=== Test: Warning State ===" << std::endl;

    ThermalShutdownController::instance().reset();

    ThermalShutdownController::instance().update_temperatures(75.0f, 70.0f);

    bool correct_state = (ThermalShutdownController::instance().get_state() == ThermalState::WARNING);
    bool not_critical = !ThermalShutdownController::instance().is_critical();
    bool no_power_reduction = !ThermalShutdownController::instance().should_reduce_power();

    std::cout << "  State: " << ThermalShutdownController::instance().get_state_string() << std::endl;
    std::cout << "  Is critical: " << (ThermalShutdownController::instance().is_critical() ? "YES" : "NO") << std::endl;
    std::cout << "  Should reduce power: " << (ThermalShutdownController::instance().should_reduce_power() ? "YES" : "NO") << std::endl;
    std::cout << "  Result: " << ((correct_state && not_critical && no_power_reduction) ? "PASS" : "FAIL") << std::endl;

    return correct_state && not_critical && no_power_reduction;
}

bool test_critical_state() {
    std::cout << "\n=== Test: Critical State ===" << std::endl;

    ThermalShutdownController::instance().reset();

    ThermalShutdownController::instance().update_temperatures(82.0f, 77.0f);

    bool correct_state = (ThermalShutdownController::instance().get_state() == ThermalState::CRITICAL);
    bool power_reduction = ThermalShutdownController::instance().should_reduce_power();

    std::cout << "  State: " << ThermalShutdownController::instance().get_state_string() << std::endl;
    std::cout << "  Should reduce power: " << (power_reduction ? "YES" : "NO") << std::endl;
    std::cout << "  Result: " << ((correct_state && power_reduction) ? "PASS" : "FAIL") << std::endl;

    return correct_state && power_reduction;
}

bool test_emergency_state() {
    std::cout << "\n=== Test: Emergency State (Below Hold Time) ===" << std::endl;

    ThermalShutdownController::instance().reset();
    ThermalShutdownController::instance().configure_defaults();

    ThermalShutdownController::instance().update_temperatures(95.0f, 88.0f);

    bool correct_state = (ThermalShutdownController::instance().get_state() == ThermalState::EMERGENCY);
    bool not_yet_shutdown = !ThermalShutdownController::instance().get_status().shutdown_triggered;

    std::cout << "  State: " << ThermalShutdownController::instance().get_state_string() << std::endl;
    std::cout << "  Shutdown triggered: " << (ThermalShutdownController::instance().get_status().shutdown_triggered ? "YES" : "NO") << std::endl;
    std::cout << "  Seconds in critical: " << ThermalShutdownController::instance().get_status().seconds_in_critical << std::endl;
    std::cout << "  Result: " << ((correct_state && not_yet_shutdown) ? "PASS" : "FAIL") << std::endl;

    return correct_state && not_yet_shutdown;
}

bool test_custom_config() {
    std::cout << "\n=== Test: Custom Configuration ===" << std::endl;

    ThermalShutdownController::instance().reset();

    ThermalConfig custom_config;
    custom_config.cpu_warning_temp = 70.0f;
    custom_config.cpu_critical_temp = 80.0f;
    custom_config.tpu_warning_temp = 65.0f;
    custom_config.tpu_critical_temp = 75.0f;
    custom_config.hold_time_seconds = 5;
    custom_config.gpio_shutdown_enabled = false;
    custom_config.gpio_pin = -1;

    ThermalShutdownController::instance().configure(custom_config);

    ThermalShutdownController::instance().update_temperatures(72.0f, 68.0f);

    bool correct_state = (ThermalShutdownController::instance().get_state() == ThermalState::CRITICAL);

    std::cout << "  CPU temp: 72°C (warning: 70°C, critical: 80°C)" << std::endl;
    std::cout << "  TPU temp: 68°C (warning: 65°C, critical: 75°C)" << std::endl;
    std::cout << "  State: " << ThermalShutdownController::instance().get_state_string() << std::endl;
    std::cout << "  Result: " << (correct_state ? "PASS" : "FAIL") << std::endl;

    return correct_state;
}

bool test_reset() {
    std::cout << "\n=== Test: Reset Functionality ===" << std::endl;

    ThermalShutdownController::instance().reset();
    ThermalShutdownController::instance().configure_defaults();

    ThermalShutdownController::instance().update_temperatures(95.0f, 88.0f);
    ThermalShutdownController::instance().reset();

    bool cpu_reset = (ThermalShutdownController::instance().get_cpu_temperature() == 0.0f);
    bool tpu_reset = (ThermalShutdownController::instance().get_tpu_temperature() == 0.0f);
    bool state_reset = (ThermalShutdownController::instance().get_state() == ThermalState::NORMAL);
    bool not_shutdown = !ThermalShutdownController::instance().get_status().shutdown_triggered;

    std::cout << "  CPU temp after reset: " << ThermalShutdownController::instance().get_cpu_temperature() << "°C" << std::endl;
    std::cout << "  State after reset: " << ThermalShutdownController::instance().get_state_string() << std::endl;
    std::cout << "  Result: " << ((cpu_reset && tpu_reset && state_reset && not_shutdown) ? "PASS" : "FAIL") << std::endl;

    return cpu_reset && tpu_reset && state_reset && not_shutdown;
}

bool test_shutdown_callback() {
    std::cout << "\n=== Test: Shutdown Callback ===" << std::endl;

    ThermalShutdownController::instance().reset();
    ThermalShutdownController::instance().configure_defaults();

    bool callback_triggered = false;
    ThermalShutdownController::instance().set_shutdown_callback([&callback_triggered]() {
        callback_triggered = true;
    });

    ThermalShutdownController::instance().trigger_shutdown();

    std::cout << "  Callback triggered: " << (callback_triggered ? "YES" : "NO") << std::endl;
    std::cout << "  Result: " << (callback_triggered ? "PASS" : "FAIL") << std::endl;

    ThermalShutdownController::instance().reset();
    return callback_triggered;
}

bool test_state_string() {
    std::cout << "\n=== Test: State String ===" << std::endl;

    ThermalShutdownController::instance().reset();

    std::string normal = ThermalShutdownController::instance().get_state_string();
    ThermalShutdownController::instance().update_temperatures(75.0f, 70.0f);
    std::string warning = ThermalShutdownController::instance().get_state_string();
    ThermalShutdownController::instance().update_temperatures(82.0f, 77.0f);
    std::string critical = ThermalShutdownController::instance().get_state_string();
    ThermalShutdownController::instance().update_temperatures(95.0f, 88.0f);
    std::string emergency = ThermalShutdownController::instance().get_state_string();

    bool all_correct = (normal == "NORMAL") && (warning == "WARNING") &&
                       (critical == "CRITICAL") && (emergency == "EMERGENCY");

    std::cout << "  NORMAL: " << normal << std::endl;
    std::cout << "  WARNING: " << warning << std::endl;
    std::cout << "  CRITICAL: " << critical << std::endl;
    std::cout << "  EMERGENCY: " << emergency << std::endl;
    std::cout << "  Result: " << (all_correct ? "PASS" : "FAIL") << std::endl;

    return all_correct;
}

int main() {
    std::cout << "================================================" << std::endl;
    std::cout << "  Thermal Shutdown Controller Tests" << std::endl;
    std::cout << "  Thresholds: CPU 80°C/90°C, TPU 75°C/85°C" << std::endl;
    std::cout << "  Hold Time: 30 seconds" << std::endl;
    std::cout << "================================================" << std::endl;

    int passed = 0, total = 0;
    auto run = [&](const char*, bool (*fn)()) {
        total++;
        if (fn()) passed++;
    };

    run("Singleton Instance", test_singleton);
    run("Default Configuration", test_default_config);
    run("Temperature Update", test_temperature_update);
    run("Warning State", test_warning_state);
    run("Critical State", test_critical_state);
    run("Emergency State", test_emergency_state);
    run("Custom Configuration", test_custom_config);
    run("Reset Functionality", test_reset);
    run("Shutdown Callback", test_shutdown_callback);
    run("State String", test_state_string);

    std::cout << "\n================================================" << std::endl;
    std::cout << "  SUMMARY: " << passed << "/" << total << " PASSED" << std::endl;
    std::cout << "================================================" << std::endl;

    ThermalShutdownController::instance().reset();
    return (passed == total) ? 0 : 1;
}
