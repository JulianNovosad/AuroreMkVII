#include "safety_monitor.hpp"
#include "../telemetry/telemetry.hpp"
#include "../monitor/shared_metrics.hpp"
#include <iostream> // Keep for std::cerr
#include <cassert>
#include <filesystem>
#include <memory>

int main() {
    std::filesystem::create_directories("test_logs");
    aurore::monitor::SharedMetrics shared_metrics;
    auto telemetry = std::make_unique<aurore::Telemetry>("test_logs", &shared_metrics);
    aurore::SafetyMonitor monitor(*telemetry);

    monitor.update();
    // std::cout << "Initial temp: " << monitor.get_cpu_temp() << std::endl; // Removed stdout
    // std::cout << "Initial usage: " << monitor.get_cpu_usage() << std::endl; // Removed stdout
    
    // Note: Since we are reading real sensors now, we can't easily mock them in a simple C++ test 
    // without refactoring for dependency injection of the sensor reading logic.
    // For now, we verify that it runs without crashing and produces some values.
    
    bool allowed = monitor.is_actuation_allowed();
    // std::cout << "Actuation allowed: " << (allowed ? "YES" : "NO") << std::endl; // Removed stdout

    // Update shared metrics for TUI if available
    {
        std::lock_guard<std::mutex> lock(shared_metrics.mutex);
        shared_metrics.cpu_temp_c.store(monitor.get_cpu_temp());
        shared_metrics.cpu_usage_percent.store(monitor.get_cpu_usage());
        // Potentially set safety_invariant_violated based on actuation allowed
        shared_metrics.safety_invariant_violated.store(!allowed);
        if (!allowed) {
            shared_metrics.safety_violation_reason = "Actuation not allowed";
        }
    }

    return 0;
}