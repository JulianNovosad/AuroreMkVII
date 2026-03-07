#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <atomic>
#include "per_frame_watchdog.h"

using namespace aurore::watchdog;

static std::atomic<int> recovery_callback_count{0};

void recovery_callback() {
    recovery_callback_count.fetch_add(1);
}

void test_watchdog_initialization() {
    std::cout << "=== Test: Watchdog Initialization ===" << std::endl;

    PerFrameWatchdog watchdog;
    assert(watchdog.get_total_stalls() == 0);
    assert(watchdog.get_frame_stalls() == 0);

    std::cout << "  Watchdog initialized: OK" << std::endl;
}

void test_stage_watchdog_recording() {
    std::cout << "=== Test: Stage Watchdog Recording ===" << std::endl;

    PerFrameWatchdog watchdog;
    StageWatchdog& capture = watchdog.get_stage(PipelineStage::CAPTURE);

    capture.record_latency(10000);
    assert(capture.last_latency_us.load() == 10000);
    assert(capture.get_stall_count() == 0);

    capture.record_latency(30000);
    assert(capture.last_latency_us.load() == 30000);
    assert(capture.get_stall_count() == 1);

    capture.record_latency(5000);
    assert(capture.last_latency_us.load() == 5000);
    assert(capture.get_stall_count() == 1);

    std::cout << "  Stage recording: OK" << std::endl;
}

void test_max_latency_tracking() {
    std::cout << "=== Test: Max Latency Tracking ===" << std::endl;

    PerFrameWatchdog watchdog;
    StageWatchdog& inference = watchdog.get_stage(PipelineStage::INFERENCE);

    inference.record_latency(10000);
    assert(inference.get_max_latency() == 10000);

    inference.record_latency(20000);
    assert(inference.get_max_latency() == 20000);

    inference.record_latency(15000);
    assert(inference.get_max_latency() == 20000);

    inference.record_latency(25000);
    assert(inference.get_max_latency() == 25000);

    std::cout << "  Max latency tracking: OK" << std::endl;
}

void test_avg_latency_calculation() {
    std::cout << "=== Test: Average Latency Calculation ===" << std::endl;

    PerFrameWatchdog watchdog;
    StageWatchdog& logic = watchdog.get_stage(PipelineStage::LOGIC);

    logic.record_latency(10000);
    logic.record_latency(20000);
    logic.record_latency(30000);

    double avg = logic.get_avg_latency_us();
    assert(avg == 20000.0);

    std::cout << "  Avg latency: " << avg << " us (expected 20000)" << std::endl;
}

void test_stall_detection() {
    std::cout << "=== Test: Stall Detection ===" << std::endl;

    PerFrameWatchdog watchdog;
    StageWatchdog& overlay = watchdog.get_stage(PipelineStage::OVERLAY);

    assert(!overlay.is_stalled());
    assert(overlay.get_stall_count() == 0);

    overlay.record_latency(10000);
    assert(!overlay.is_stalled());

    overlay.record_latency(26000);
    assert(overlay.is_stalled());
    assert(overlay.get_stall_count() == 1);

    overlay.record_latency(5000);
    assert(!overlay.is_stalled());

    std::cout << "  Stall detection: OK" << std::endl;
}

void test_total_stall_count() {
    std::cout << "=== Test: Total Stall Count ===" << std::endl;

    PerFrameWatchdog watchdog;

    watchdog.record_stage_latency(PipelineStage::CAPTURE, 10000);
    assert(watchdog.get_total_stalls() == 0);

    watchdog.record_stage_latency(PipelineStage::INFERENCE, 30000);
    assert(watchdog.get_total_stalls() == 1);

    watchdog.record_stage_latency(PipelineStage::LOGIC, 26000);
    assert(watchdog.get_total_stalls() == 2);

    watchdog.record_stage_latency(PipelineStage::OVERLAY, 5000);
    assert(watchdog.get_total_stalls() == 2);

    std::cout << "  Total stall count: OK" << std::endl;
}

void test_pipeline_health_check() {
    std::cout << "=== Test: Pipeline Health Check ===" << std::endl;

    PerFrameWatchdog watchdog;

    watchdog.record_stage_latency(PipelineStage::CAPTURE, 10000);
    watchdog.record_stage_latency(PipelineStage::INFERENCE, 15000);
    watchdog.record_stage_latency(PipelineStage::LOGIC, 5000);

    assert(watchdog.check_pipeline_health());

    watchdog.record_stage_latency(PipelineStage::DISPLAY, 26000);

    assert(!watchdog.check_pipeline_health());

    std::cout << "  Pipeline health check: OK" << std::endl;
}

void test_frame_latency_recording() {
    std::cout << "=== Test: Frame Latency Recording ===" << std::endl;

    PerFrameWatchdog watchdog;

    watchdog.record_frame_latency(20000);
    assert(watchdog.get_total_stalls() == 0);

    watchdog.record_frame_latency(35000);
    assert(watchdog.get_total_stalls() == 0);

    std::cout << "  Frame latency recording: OK" << std::endl;
}

void test_reset_functionality() {
    std::cout << "=== Test: Reset Functionality ===" << std::endl;

    PerFrameWatchdog watchdog;

    watchdog.record_stage_latency(PipelineStage::CAPTURE, 30000);
    watchdog.record_stage_latency(PipelineStage::INFERENCE, 26000);

    assert(watchdog.get_total_stalls() > 0);
    assert(watchdog.get_stage(PipelineStage::CAPTURE).get_stall_count() > 0);

    watchdog.reset_all();

    assert(watchdog.get_total_stalls() == 0);
    assert(watchdog.get_frame_stalls() == 0);
    assert(watchdog.get_stage(PipelineStage::CAPTURE).get_stall_count() == 0);
    assert(watchdog.get_stage(PipelineStage::CAPTURE).get_max_latency() == 0);

    std::cout << "  Reset functionality: OK" << std::endl;
}

void test_recovery_callback() {
    std::cout << "=== Test: Recovery Callback ===" << std::endl;

    PerFrameWatchdog watchdog;
    watchdog.set_recovery_callback(recovery_callback);
    recovery_callback_count.store(0);

    watchdog.record_stage_latency(PipelineStage::CAPTURE, 10000);
    assert(recovery_callback_count.load() == 0);

    watchdog.record_stage_latency(PipelineStage::INFERENCE, 26000);
    assert(recovery_callback_count.load() == 0);

    watchdog.record_stage_latency(PipelineStage::LOGIC, 26000);
    assert(recovery_callback_count.load() == 0);

    watchdog.record_stage_latency(PipelineStage::OVERLAY, 26000);
    assert(recovery_callback_count.load() == 1);

    watchdog.record_stage_latency(PipelineStage::DISPLAY, 26000);
    assert(recovery_callback_count.load() == 2);

    std::cout << "  Recovery callback: OK (called " << recovery_callback_count.load() << " times)" << std::endl;
}

void test_health_report_generation() {
    std::cout << "=== Test: Health Report Generation ===" << std::endl;

    PerFrameWatchdog watchdog;

    watchdog.record_stage_latency(PipelineStage::CAPTURE, 5000);
    watchdog.record_stage_latency(PipelineStage::INFERENCE, 10000);
    watchdog.record_stage_latency(PipelineStage::LOGIC, 8000);
    watchdog.record_frame_latency(23000);

    std::string report = watchdog.get_health_report();

    assert(report.find("Per-Frame Watchdog Health Report") != std::string::npos);
    assert(report.find("Capture") != std::string::npos);
    assert(report.find("Inference") != std::string::npos);

    std::cout << "  Health report generation: OK" << std::endl;
    std::cout << report << std::endl;
}

void test_stage_names() {
    std::cout << "=== Test: Stage Names ===" << std::endl;

    assert(std::string(PerFrameWatchdog::stage_name(PipelineStage::CAPTURE)) == "Capture");
    assert(std::string(PerFrameWatchdog::stage_name(PipelineStage::INFERENCE)) == "Inference");
    assert(std::string(PerFrameWatchdog::stage_name(PipelineStage::LOGIC)) == "Logic");
    assert(std::string(PerFrameWatchdog::stage_name(PipelineStage::OVERLAY)) == "Overlay");
    assert(std::string(PerFrameWatchdog::stage_name(PipelineStage::DISPLAY)) == "Display");

    assert(PerFrameWatchdog::parse_stage("Capture") == PipelineStage::CAPTURE);
    assert(PerFrameWatchdog::parse_stage("Inference") == PipelineStage::INFERENCE);
    assert(PerFrameWatchdog::parse_stage("Logic") == PipelineStage::LOGIC);

    std::cout << "  Stage names: OK" << std::endl;
}

void test_concurrent_latency_recording() {
    std::cout << "=== Test: Concurrent Latency Recording ===" << std::endl;

    PerFrameWatchdog watchdog;

    std::thread t1([&watchdog]() {
        for (int i = 0; i < 100; ++i) {
            watchdog.record_stage_latency(PipelineStage::CAPTURE, 5000 + (i % 10) * 1000);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    std::thread t2([&watchdog]() {
        for (int i = 0; i < 100; ++i) {
            watchdog.record_stage_latency(PipelineStage::INFERENCE, 8000 + (i % 10) * 1000);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    t1.join();
    t2.join();

    assert(watchdog.get_stage(PipelineStage::CAPTURE).get_stall_count() == 0);
    assert(watchdog.get_stage(PipelineStage::INFERENCE).get_stall_count() == 0);

    std::cout << "  Concurrent recording: OK" << std::endl;
}

int main() {
    std::cout << "================================================" << std::endl;
    std::cout << "  Per-Frame Watchdog Test Suite" << std::endl;
    std::cout << "  Stall threshold: 25ms" << std::endl;
    std::cout << "================================================" << std::endl;

    int passed = 0;
    int total = 0;

    auto run_test = [&](const char* name, void (*test)()) {
        total++;
        try {
            test();
            passed++;
            std::cout << "  [PASS] " << name << std::endl;
        } catch (const std::exception& e) {
            std::cout << "  [FAIL] " << name << ": " << e.what() << std::endl;
        }
    };

    run_test("Watchdog Initialization", test_watchdog_initialization);
    run_test("Stage Watchdog Recording", test_stage_watchdog_recording);
    run_test("Max Latency Tracking", test_max_latency_tracking);
    run_test("Average Latency Calculation", test_avg_latency_calculation);
    run_test("Stall Detection", test_stall_detection);
    run_test("Total Stall Count", test_total_stall_count);
    run_test("Pipeline Health Check", test_pipeline_health_check);
    run_test("Frame Latency Recording", test_frame_latency_recording);
    run_test("Reset Functionality", test_reset_functionality);
    run_test("Recovery Callback", test_recovery_callback);
    run_test("Health Report Generation", test_health_report_generation);
    run_test("Stage Names", test_stage_names);
    run_test("Concurrent Latency Recording", test_concurrent_latency_recording);

    std::cout << "\n================================================" << std::endl;
    std::cout << "  SUMMARY: " << passed << "/" << total << " PASSED" << std::endl;
    std::cout << "================================================" << std::endl;

    return (passed == total) ? 0 : 1;
}
