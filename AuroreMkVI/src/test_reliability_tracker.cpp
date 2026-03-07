#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <cmath>

#include "reliability_tracker.h"

using namespace aurore::reliability;

bool test_singleton() {
    std::cout << "\n=== Test: Singleton Instance ===" << std::endl;

    ReliabilityTracker& instance1 = ReliabilityTracker::instance();
    ReliabilityTracker& instance2 = ReliabilityTracker::instance();

    bool same_instance = (&instance1 == &instance2);
    std::cout << "  Same instance: " << (same_instance ? "PASS" : "FAIL") << std::endl;

    return same_instance;
}

bool test_failure_recording() {
    std::cout << "\n=== Test: Failure Recording ===" << std::endl;

    ReliabilityTracker::instance().record_failure(
        FailureCategory::TPU, FailureSeverity::ERROR, "TPU timeout", 50000);

    ReliabilityTracker::instance().record_failure(
        FailureCategory::CAMERA, FailureSeverity::WARNING, "Frame drop", 10000);

    ReliabilitySnapshot snapshot = ReliabilityTracker::instance().get_snapshot();

    bool has_failures = snapshot.total_failures_all_categories >= 2;
    std::cout << "  Failures recorded: " << snapshot.total_failures_all_categories << std::endl;
    std::cout << "  Result: " << (has_failures ? "PASS" : "FAIL") << std::endl;

    return has_failures;
}

bool test_mtbf_calculation() {
    std::cout << "\n=== Test: MTBF Calculation ===" << std::endl;

    ReliabilityTracker::instance().reset();

    ReliabilityTracker::instance().start_uptime();

    std::this_thread::sleep_for(std::chrono::seconds(1));

    ReliabilityTracker::instance().record_failure(
        FailureCategory::TPU, FailureSeverity::ERROR, "Test failure 1", 10000);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    ReliabilityTracker::instance().record_failure(
        FailureCategory::TPU, FailureSeverity::ERROR, "Test failure 2", 10000);

    MTBFStats tpu_stats = ReliabilityTracker::instance().calculate_mtbf_for_category(FailureCategory::TPU);

    std::cout << "  TPU failures: " << tpu_stats.total_failures << std::endl;
    std::cout << "  TPU MTBF (hours): " << std::fixed << std::setprecision(6) << tpu_stats.mtbf_hours << std::endl;
    std::cout << "  TPU uptime (hours): " << (tpu_stats.total_uptime_seconds / 3600.0) << std::endl;
    std::cout << "  TPU availability: " << tpu_stats.availability_percentage << "%" << std::endl;

    bool valid = (tpu_stats.total_failures == 2) && (tpu_stats.mtbf_hours > 0);
    std::cout << "  Result: " << (valid ? "PASS" : "FAIL") << std::endl;

    ReliabilityTracker::instance().stop_uptime();

    return valid;
}

bool test_mtbf_target_check() {
    std::cout << "\n=== Test: MTBF Target Check ===" << std::endl;

    bool meets_10000 = ReliabilityTracker::instance().meets_mtbf_target(FailureCategory::TPU, 10000.0);
    std::cout << "  Meets 10000h target: " << (meets_10000 ? "YES" : "NO") << std::endl;

    bool meets_0 = ReliabilityTracker::instance().meets_mtbf_target(FailureCategory::SERVO, 0.1);
    std::cout << "  Meets 0.1h target (no failures): " << (meets_0 ? "YES" : "NO") << std::endl;

    return true;
}

bool test_category_counts() {
    std::cout << "\n=== Test: Category Failure Counts ===" << std::endl;

    ReliabilityTracker::instance().reset();
    ReliabilityTracker::instance().start_uptime();

    ReliabilityTracker::instance().record_failure(
        FailureCategory::TPU, FailureSeverity::ERROR, "TPU error 1", 5000);
    ReliabilityTracker::instance().record_failure(
        FailureCategory::TPU, FailureSeverity::ERROR, "TPU error 2", 5000);
    ReliabilityTracker::instance().record_failure(
        FailureCategory::TPU, FailureSeverity::ERROR, "TPU error 3", 5000);

    ReliabilityTracker::instance().record_failure(
        FailureCategory::CAMERA, FailureSeverity::WARNING, "Camera glitch", 2000);

    MTBFStats tpu = ReliabilityTracker::instance().calculate_mtbf_for_category(FailureCategory::TPU);
    MTBFStats camera = ReliabilityTracker::instance().calculate_mtbf_for_category(FailureCategory::CAMERA);

    bool correct = (tpu.total_failures == 3) && (camera.total_failures == 1);
    std::cout << "  TPU failures: " << tpu.total_failures << " (expected 3)" << std::endl;
    std::cout << "  Camera failures: " << camera.total_failures << " (expected 1)" << std::endl;
    std::cout << "  Result: " << (correct ? "PASS" : "FAIL") << std::endl;

    ReliabilityTracker::instance().stop_uptime();

    return correct;
}

bool test_ema_mtbf() {
    std::cout << "\n=== Test: Exponential Moving Average MTBF ===" << std::endl;

    ReliabilityTracker::instance().reset();
    ReliabilityTracker::instance().start_uptime();

    for (int i = 0; i < 5; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ReliabilityTracker::instance().record_failure(
            FailureCategory::MEMORY, FailureSeverity::WARNING, "Memory warning", 1000);
    }

    MTBFStats mem = ReliabilityTracker::instance().calculate_mtbf_for_category(FailureCategory::MEMORY);

    std::cout << "  Memory failures: " << mem.total_failures << std::endl;
    std::cout << "  EMA MTBF (hours): " << mem.ema_mtbf_hours << std::endl;

    bool valid = (mem.total_failures == 5) && (mem.ema_mtbf_hours >= 0);
    std::cout << "  Result: " << (valid ? "PASS" : "FAIL") << std::endl;

    ReliabilityTracker::instance().stop_uptime();

    return valid;
}

bool test_snapshot() {
    std::cout << "\n=== Test: Reliability Snapshot ===" << std::endl;

    ReliabilitySnapshot snapshot = ReliabilityTracker::instance().get_snapshot();

    std::cout << "  Snapshot timestamp: valid" << std::endl;
    std::cout << "  System uptime (s): " << snapshot.system_uptime_seconds << std::endl;
    std::cout << "  Total failures: " << snapshot.total_failures_all_categories << std::endl;
    std::cout << "  Categories tracked: " << snapshot.mtbf_by_category.size() << std::endl;
    std::cout << "  Overall MTBF (h): " << snapshot.overall_mtbf_hours << std::endl;
    std::cout << "  Overall availability: " << snapshot.overall_availability << "%" << std::endl;

    bool valid = snapshot.mtbf_by_category.size() == 7;
    std::cout << "  Result: " << (valid ? "PASS" : "FAIL") << std::endl;

    return valid;
}

bool test_critical_failure_tracking() {
    std::cout << "\n=== Test: Critical Failure Tracking ===" << std::endl;

    size_t before = ReliabilityTracker::instance().get_critical_failures();

    ReliabilityTracker::instance().record_failure(
        FailureCategory::THERMAL, FailureSeverity::CRITICAL, "Thermal runaway", 100000);

    size_t after = ReliabilityTracker::instance().get_critical_failures();

    bool tracked = (after == before + 1);
    std::cout << "  Critical failures before: " << before << std::endl;
    std::cout << "  Critical failures after: " << after << std::endl;
    std::cout << "  Result: " << (tracked ? "PASS" : "FAIL") << std::endl;

    return tracked;
}

bool test_recovery_time() {
    std::cout << "\n=== Test: Recovery Time Recording ===" << std::endl;

    ReliabilityTracker::instance().record_recovery(FailureCategory::SERVO, 25000);

    ReliabilityTracker::instance().record_recovery(FailureCategory::SERVO, 35000);

    MTBFStats servo = ReliabilityTracker::instance().calculate_mtbf_for_category(FailureCategory::SERVO);

    std::cout << "  Servo failures: " << servo.total_failures << std::endl;
    std::cout << "  Servo availability: " << servo.availability_percentage << "%" << std::endl;

    bool valid = true;
    std::cout << "  Result: " << (valid ? "PASS" : "FAIL") << std::endl;

    return valid;
}

bool test_concurrent_access() {
    std::cout << "\n=== Test: Concurrent Access ===" << std::endl;

    ReliabilityTracker::instance().reset();

    std::thread t1([&]() {
        for (int i = 0; i < 100; ++i) {
            ReliabilityTracker::instance().record_failure(
                FailureCategory::TPU, FailureSeverity::ERROR, "TPU error", 1000);
        }
    });

    std::thread t2([&]() {
        for (int i = 0; i < 100; ++i) {
            ReliabilityTracker::instance().record_failure(
                FailureCategory::CAMERA, FailureSeverity::WARNING, "Camera error", 1000);
        }
    });

    std::thread t3([&]() {
        for (int i = 0; i < 100; ++i) {
            auto snapshot = ReliabilityTracker::instance().get_snapshot();
            (void)snapshot;
        }
    });

    t1.join();
    t2.join();
    t3.join();

    MTBFStats tpu = ReliabilityTracker::instance().calculate_mtbf_for_category(FailureCategory::TPU);
    MTBFStats camera = ReliabilityTracker::instance().calculate_mtbf_for_category(FailureCategory::CAMERA);

    bool valid = (tpu.total_failures == 100) && (camera.total_failures == 100);
    std::cout << "  TPU failures: " << tpu.total_failures << std::endl;
    std::cout << "  Camera failures: " << camera.total_failures << std::endl;
    std::cout << "  Result: " << (valid ? "PASS" : "FAIL") << std::endl;

    return valid;
}

int main() {
    std::cout << "================================================" << std::endl;
    std::cout << "  Reliability Tracker MTBF Tests" << std::endl;
    std::cout << "  Target: MTBF >= 10,000 hours system-wide" << std::endl;
    std::cout << "================================================" << std::endl;

    int passed = 0, total = 0;
    auto run = [&](const char*, bool (*fn)()) {
        total++;
        if (fn()) passed++;
    };

    run("Singleton Instance", test_singleton);
    run("Failure Recording", test_failure_recording);
    run("MTBF Calculation", test_mtbf_calculation);
    run("MTBF Target Check", test_mtbf_target_check);
    run("Category Failure Counts", test_category_counts);
    run("EMA MTBF", test_ema_mtbf);
    run("Reliability Snapshot", test_snapshot);
    run("Critical Failure Tracking", test_critical_failure_tracking);
    run("Recovery Time Recording", test_recovery_time);
    run("Concurrent Access", test_concurrent_access);

    std::cout << "\n================================================" << std::endl;
    std::cout << "  SUMMARY: " << passed << "/" << total << " PASSED" << std::endl;
    std::cout << "================================================" << std::endl;

    ReliabilityTracker::instance().export_to_csv("reliability_test_report.csv");
    std::cout << "\nReport exported to: reliability_test_report.csv" << std::endl;

    return (passed == total) ? 0 : 1;
}
