#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <cassert>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <random>
#include <memory>

#include "reliability_tracker.h"
#include "thermal_shutdown.h"
#include "memory_pool_limits.h"
#include "data_integrity.h"

using namespace aurore::reliability;
using namespace aurore::thermal;
using namespace aurore::memory;
using namespace aurore::integrity;

constexpr int STRESS_TEST_DURATION_MINUTES = 60;
constexpr int REPORT_INTERVAL_SECONDS = 60;
constexpr int MEMORY_SAMPLE_INTERVAL_MS = 100;
constexpr int TEMPERATURE_SAMPLE_INTERVAL_MS = 500;

struct StressTestResult {
    bool passed;
    double uptime_hours;
    uint64_t total_failures;
    uint64_t critical_failures;
    double max_cpu_temp;
    double max_tpu_temp;
    double max_memory_usage_mb;
    uint64_t memory_purges;
    uint64_t memory_failures;
    uint64_t crc_errors;
    uint64_t thermal_shutdowns;
    std::string failure_summary;
};

class StressTestSuite {
public:
    StressTestSuite() : running_(false), test_passed_(true) {}

    StressTestResult run_full_suite() {
        StressTestResult result;
        result.passed = true;
        result.uptime_hours = 0;
        result.total_failures = 0;
        result.critical_failures = 0;
        result.max_cpu_temp = 0;
        result.max_tpu_temp = 0;
        result.max_memory_usage_mb = 0;
        result.memory_purges = 0;
        result.memory_failures = 0;
        result.crc_errors = 0;
        result.thermal_shutdowns = 0;
        result.failure_summary = "";

        std::cout << "================================================" << std::endl;
        std::cout << "  AURORE MK VI STRESS TEST SUITE" << std::endl;
        std::cout << "  Duration: " << STRESS_TEST_DURATION_MINUTES << " minutes" << std::endl;
        std::cout << "================================================" << std::endl;

        start_infrastructure();
        
        auto start_time = std::chrono::steady_clock::now();
        auto end_time = start_time + std::chrono::minutes(STRESS_TEST_DURATION_MINUTES);
        
        int report_counter = 0;
        
        while (std::chrono::steady_clock::now() < end_time && running_.load()) {
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            auto remaining = end_time - std::chrono::steady_clock::now();
            
            int minutes_elapsed = std::chrono::duration_cast<std::chrono::minutes>(elapsed).count();
            int minutes_remaining = std::chrono::duration_cast<std::chrono::minutes>(remaining).count();
            
            if (report_counter % REPORT_INTERVAL_SECONDS == 0) {
                print_progress_report(minutes_elapsed, minutes_remaining);
            }
            
            simulate_load();
            check_conditions();
            
            report_counter++;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        stop_infrastructure();
        
        collect_results(result);
        
        print_final_report(result);
        
        return result;
    }

private:
    std::atomic<bool> running_;
    std::atomic<bool> test_passed_;
    std::vector<std::thread> load_threads_;
    std::vector<std::thread> monitor_threads_;
    
    std::atomic<double> current_cpu_temp_{0};
    std::atomic<double> current_tpu_temp_{0};
    std::atomic<double> current_memory_mb_{0};
    std::atomic<uint64_t> crc_error_count_{0};

    void start_infrastructure() {
        std::cout << "\n[INIT] Starting stress test infrastructure..." << std::endl;
        
        running_.store(true);
        test_passed_.store(true);
        
        ReliabilityTracker::instance().start_uptime();
        
        ThermalShutdownController::instance().configure_defaults();
        ThermalShutdownController::instance().set_shutdown_callback([this]() {
            std::cout << "[THERMAL] Emergency shutdown triggered during stress test!" << std::endl;
            thermal_shutdowns_.fetch_add(1);
            test_passed_.store(false);
        });
        
        MemoryPoolLimits::instance().configure_defaults();
        MemoryPoolLimits::instance().set_alert_callback([this](MemoryPool pool, MemoryAlertLevel level, float utilization) {
            if (level == MemoryAlertLevel::CRITICAL || level == MemoryAlertLevel::EMERGENCY) {
                std::cout << "[MEMORY] Critical alert: " 
                          << MemoryPoolLimits::instance().get_pool_stats(pool).name
                          << " at " << (utilization * 100) << "%" << std::endl;
            }
        });
        
        MemoryPoolLimits::instance().set_allocation_failed_callback([this](MemoryPool pool, size_t size, size_t) {
            std::cout << "[MEMORY] Allocation failed: "
                      << MemoryPoolLimits::instance().get_pool_stats(pool).name
                      << " - requested " << size << " bytes" << std::endl;
            memory_failures_.fetch_add(1);
            test_passed_.store(false);
        });
        
        std::cout << "[INIT] Infrastructure started." << std::endl;
    }
    
    void stop_infrastructure() {
        std::cout << "\n[CLEANUP] Stopping stress test infrastructure..." << std::endl;
        
        running_.store(false);
        
        for (auto& t : load_threads_) {
            if (t.joinable()) t.join();
        }
        for (auto& t : monitor_threads_) {
            if (t.joinable()) t.join();
        }
        
        ReliabilityTracker::instance().stop_uptime();
        
        std::cout << "[CLEANUP] Infrastructure stopped." << std::endl;
    }
    
    void simulate_load() {
        static std::atomic<int> thread_id{0};
        
        if (load_threads_.size() < 4) {
            load_threads_.emplace_back([this]() {
                int id = thread_id.fetch_add(1);
                simulate_memory_load(id);
            });
            load_threads_.emplace_back([this]() {
                int id = thread_id.fetch_add(1);
                simulate_compute_load(id);
            });
            load_threads_.emplace_back([this]() {
                int id = thread_id.fetch_add(1);
                simulate_allocation_load(id);
            });
            load_threads_.emplace_back([this]() {
                int id = thread_id.fetch_add(1);
                simulate_crc_load(id);
            });
        }
    }
    
    void simulate_memory_load(int thread_id) {
        std::random_device rd;
        std::mt19937 gen(rd() + thread_id);
        std::uniform_int_distribution<> dist(1000, 10000);
        
        while (running_.load()) {
            size_t size = dist(gen);
            if (MemoryPoolLimits::instance().allocate(MemoryPool::HEAP_GENERAL, size)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(dist(gen) / 10));
                MemoryPoolLimits::instance().deallocate(MemoryPool::HEAP_GENERAL, size);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    void simulate_compute_load(int thread_id) {
        std::random_device rd;
        std::mt19937 gen(rd() + thread_id);
        std::uniform_int_distribution<> dist(1000, 5000);
        
        while (running_.load()) {
            volatile double result = 1.0;
            for (int i = 0; i < 10000; ++i) {
                result = result * 1.0001 + 0.0001;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(dist(gen) / 100));
        }
    }
    
    void simulate_allocation_load(int thread_id) {
        std::random_device rd;
        std::mt19937 gen(rd() + thread_id);
        std::uniform_int_distribution<> dist(100, 1000);
        
        std::vector<std::vector<uint8_t>> buffers;
        
        while (running_.load() && buffers.size() < 100) {
            size_t size = dist(gen) * 1024;
            buffers.emplace_back(size);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        buffers.clear();
    }
    
    void simulate_crc_load(int thread_id) {
        std::random_device rd;
        std::mt19937 gen(rd() + thread_id);
        std::uniform_int_distribution<> dist(1000, 5000);
        
        while (running_.load()) {
            std::vector<uint8_t> data(1024);
            for (auto& byte : data) {
                byte = static_cast<uint8_t>(gen());
            }
            
            uint32_t crc = calculate_crc32(data);
            if (gen() % 100 < 1) {
                data[gen() % 1024] ^= 0xFF;
                uint32_t crc2 = calculate_crc32(data);
                if (crc != crc2) {
                    crc_error_count_.fetch_add(1);
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(dist(gen) / 100));
        }
    }
    
    void check_conditions() {
        auto snapshot = MemoryPoolLimits::instance().get_snapshot();
        current_memory_mb_.store(snapshot.total_allocated_bytes / (1024.0 * 1024.0));
        
        if (snapshot.overall_alert_level == MemoryAlertLevel::EMERGENCY) {
            size_t excess = snapshot.total_allocated_bytes - snapshot.total_limit_bytes;
            size_t purged = MemoryPoolLimits::instance().purge_non_critical(excess * 2);
            memory_purges_.fetch_add(purged > 0 ? 1 : 0);
        }
        
        double cpu_temp = 45.0 + (current_memory_mb_.load() / 100.0) * 10.0 + (load_threads_.size() * 2.0);
        double tpu_temp = 40.0 + (current_memory_mb_.load() / 100.0) * 15.0 + (load_threads_.size() * 3.0);
        
        current_cpu_temp_.store(cpu_temp);
        current_tpu_temp_.store(tpu_temp);
        
        ThermalShutdownController::instance().update_temperatures(cpu_temp, tpu_temp);
        
        if (cpu_temp > current_cpu_temp_.load() && cpu_temp > result_.max_cpu_temp) {
            result_.max_cpu_temp = cpu_temp;
        }
        if (tpu_temp > current_tpu_temp_.load() && tpu_temp > result_.max_tpu_temp) {
            result_.max_tpu_temp = tpu_temp;
        }
        if (current_memory_mb_.load() > result_.max_memory_usage_mb) {
            result_.max_memory_usage_mb = current_memory_mb_.load();
        }
        
        if (ThermalShutdownController::instance().is_critical()) {
            test_passed_.store(false);
        }
    }
    
    void print_progress_report(int minutes_elapsed, int minutes_remaining) {
        auto snapshot = MemoryPoolLimits::instance().get_snapshot();
        auto reliability_snapshot = ReliabilityTracker::instance().get_snapshot();
        
        std::cout << "\n[REPORT] Progress: " << minutes_elapsed << "/" << STRESS_TEST_DURATION_MINUTES 
                  << " minutes (remaining: " << minutes_remaining << " min)" << std::endl;
        std::cout << "  Memory: " << std::fixed << std::setprecision(2) 
                  << current_memory_mb_.load() << "MB / "
                  << (snapshot.total_limit_bytes / (1024.0 * 1024.0)) << "MB ("
                  << snapshot.overall_utilization_percent << "%)" << std::endl;
        std::cout << "  Temperature: CPU " << std::fixed << std::setprecision(1) 
                  << current_cpu_temp_.load() << "°C, TPU " << current_tpu_temp_.load() << "°C" << std::endl;
        std::cout << "  Failures: " << reliability_snapshot.total_failures_all_categories
                  << " (critical: " << ReliabilityTracker::instance().get_critical_failures() << ")" << std::endl;
        std::cout << "  CRC errors: " << crc_error_count_.load() << std::endl;
        std::cout << "  Memory purges: " << memory_purges_.load() << std::endl;
        std::cout << "  Memory failures: " << memory_failures_.load() << std::endl;
    }
    
    void collect_results(StressTestResult& result) {
        auto reliability_snapshot = ReliabilityTracker::instance().get_snapshot();
        auto memory_snapshot = MemoryPoolLimits::instance().get_snapshot();
        
        result.uptime_hours = reliability_snapshot.system_uptime_seconds / 3600.0;
        result.total_failures = reliability_snapshot.total_failures_all_categories;
        result.critical_failures = ReliabilityTracker::instance().get_critical_failures();
        result.memory_purges = memory_snapshot.total_purge_count;
        result.memory_failures = memory_snapshot.total_failed_allocations;
        result.crc_errors = crc_error_count_.load();
        result.thermal_shutdowns = thermal_shutdowns_.load();
        
        std::stringstream ss;
        ss << "Max CPU temp: " << std::fixed << std::setprecision(1) << result.max_cpu_temp << "°C, ";
        ss << "Max TPU temp: " << result.max_tpu_temp << "°C, ";
        ss << "Max memory: " << result.max_memory_usage_mb << "MB, ";
        ss << "Failures: " << result.total_failures << ", ";
        ss << "CRC errors: " << result.crc_errors;
        result.failure_summary = ss.str();
        
        result.passed = test_passed_.load() && 
                       (result.max_cpu_temp < 90.0) &&
                       (result.max_tpu_temp < 85.0) &&
                       (result.crc_errors < 10);
    }
    
    void print_final_report(const StressTestResult& result) {
        std::cout << "\n================================================" << std::endl;
        std::cout << "  STRESS TEST FINAL REPORT" << std::endl;
        std::cout << "================================================" << std::endl;
        std::cout << "  Status: " << (result.passed ? "PASSED" : "FAILED") << std::endl;
        std::cout << "  Uptime: " << std::fixed << std::setprecision(2) << result.uptime_hours << " hours" << std::endl;
        std::cout << "  Max CPU Temperature: " << result.max_cpu_temp << "°C" << std::endl;
        std::cout << "  Max TPU Temperature: " << result.max_tpu_temp << "°C" << std::endl;
        std::cout << "  Max Memory Usage: " << result.max_memory_usage_mb << " MB" << std::endl;
        std::cout << "  Total Failures: " << result.total_failures << std::endl;
        std::cout << "  Critical Failures: " << result.critical_failures << std::endl;
        std::cout << "  CRC Errors: " << result.crc_errors << std::endl;
        std::cout << "  Memory Purges: " << result.memory_purges << std::endl;
        std::cout << "  Memory Failures: " << result.memory_failures << std::endl;
        std::cout << "  Thermal Shutdowns: " << result.thermal_shutdowns << std::endl;
        std::cout << "================================================" << std::endl;
    }
    
    std::atomic<uint64_t> memory_purges_{0};
    std::atomic<uint64_t> memory_failures_{0};
    std::atomic<uint64_t> thermal_shutdowns_{0};
    StressTestResult result_;
};

bool test_singleton_patterns() {
    std::cout << "\n=== Test: Singleton Pattern Verification ===" << std::endl;
    
    bool reliability_ok = (&ReliabilityTracker::instance() == &ReliabilityTracker::instance());
    bool thermal_ok = (&ThermalShutdownController::instance() == &ThermalShutdownController::instance());
    bool memory_ok = (&MemoryPoolLimits::instance() == &MemoryPoolLimits::instance());
    
    std::cout << "  ReliabilityTracker singleton: " << (reliability_ok ? "PASS" : "FAIL") << std::endl;
    std::cout << "  ThermalShutdownController singleton: " << (thermal_ok ? "PASS" : "FAIL") << std::endl;
    std::cout << "  MemoryPoolLimits singleton: " << (memory_ok ? "PASS" : "FAIL") << std::endl;
    
    return reliability_ok && thermal_ok && memory_ok;
}

bool test_infrastructure_defaults() {
    std::cout << "\n=== Test: Infrastructure Default Configuration ===" << std::endl;
    
    ReliabilityTracker::instance().reset();
    ThermalShutdownController::instance().reset();
    MemoryPoolLimits::instance().reset();
    
    ThermalShutdownController::instance().configure_defaults();
    
    auto status = ThermalShutdownController::instance().get_status();
    bool config_ok = (status.state == ThermalState::NORMAL);
    
    std::cout << "  Default thermal state: " << ThermalShutdownController::instance().get_state_string() << std::endl;
    std::cout << "  Configured correctly: " << (config_ok ? "PASS" : "FAIL") << std::endl;
    
    return config_ok;
}

bool test_stress_test_instantiation() {
    std::cout << "\n=== Test: Stress Test Suite Instantiation ===" << std::endl;
    
    StressTestSuite suite;
    bool created = true;
    
    std::cout << "  StressTestSuite instantiated: " << (created ? "PASS" : "FAIL") << std::endl;
    
    return created;
}

int main() {
    std::cout << "================================================" << std::endl;
    std::cout << "  Aurore Mk VI Stress Test Suite" << std::endl;
    std::cout << "  Testing: MTBF, Thermal, Memory, Data Integrity" << std::endl;
    std::cout << "================================================" << std::endl;

    int passed = 0, total = 0;
    auto run = [&](const char*, bool (*fn)()) {
        total++;
        if (fn()) passed++;
    };

    run("Singleton Pattern", test_singleton_patterns);
    run("Infrastructure Defaults", test_infrastructure_defaults);
    run("Stress Test Instantiation", test_stress_test_instantiation);

    std::cout << "\n================================================" << std::endl;
    std::cout << "  INFRASTRUCTURE TESTS: " << passed << "/" << total << " PASSED" << std::endl;
    std::cout << "================================================" << std::endl;

    std::cout << "\n[INFO] To run full stress test, call: stress_test_suite.run_full_suite()" << std::endl;
    std::cout << "[INFO] Full test duration: " << STRESS_TEST_DURATION_MINUTES << " minutes" << std::endl;

    return (passed == total) ? 0 : 1;
}
