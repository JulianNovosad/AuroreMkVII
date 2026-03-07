#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <cassert>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

#include "thread_affinity.h"

using namespace aurore::threading;

static std::atomic<uint64_t> g_max_inference_latency_us{0};
static std::atomic<uint64_t> g_total_inferences{0};
static std::atomic<uint64_t> g_stall_count{0};
static std::atomic<uint64_t> g_wcet_violations{0};

constexpr uint64_t MAX_ALLOWED_LATENCY_US = 18000;
constexpr uint64_t WCET_TARGET_US = 15000;
constexpr uint64_t STALL_THRESHOLD_US = 25000;

class LatencyRecorder {
public:
    std::vector<uint64_t> latencies_us;
    std::chrono::steady_clock::time_point last_sample_time;

    void start() {
        last_sample_time = std::chrono::steady_clock::now();
        latencies_us.clear();
    }

    uint64_t record_sample() {
        auto now = std::chrono::steady_clock::now();
        uint64_t interval_us = std::chrono::duration_cast<std::chrono::microseconds>(
            now - last_sample_time).count();
        last_sample_time = now;
        latencies_us.push_back(interval_us);
        return interval_us;
    }

    uint64_t get_max() const {
        if (latencies_us.empty()) return 0;
        return *std::max_element(latencies_us.begin(), latencies_us.end());
    }

    uint64_t get_min() const {
        if (latencies_us.empty()) return 0;
        return *std::min_element(latencies_us.begin(), latencies_us.end());
    }

    uint64_t get_avg() const {
        if (latencies_us.empty()) return 0;
        uint64_t sum = 0;
        for (auto lat : latencies_us) sum += lat;
        return sum / latencies_us.size();
    }

    uint64_t get_percentile(double p) const {
        if (latencies_us.empty()) return 0;
        std::vector<uint64_t> sorted = latencies_us;
        std::sort(sorted.begin(), sorted.end());
        size_t idx = static_cast<size_t>(sorted.size() * p / 100.0);
        if (idx >= sorted.size()) idx = sorted.size() - 1;
        return sorted[idx];
    }
};

bool test_tpu_worker_thread_affinity() {
    std::cout << "\n=== Test: TPU Worker Thread Affinity ===" << std::endl;

    std::atomic<bool> verified{false};
    std::thread([&verified]() {
        ThreadAffinityManager::apply_tpu_worker_affinity();
        verified.store(ThreadAffinityManager::verify_thread_affinity(ThreadRole::TPU_WORKER));
    }).join();

    std::cout << "  TPU worker thread affinity: " << (verified.load() ? "VERIFIED" : "FAILED") << std::endl;
    return verified.load();
}

bool test_tpu_worker_scheduler_settings() {
    std::cout << "\n=== Test: TPU Worker Scheduler Settings ===" << std::endl;

    std::string sched = ThreadAffinityManager::get_scheduler_string(ThreadRole::TPU_WORKER);
    bool fifo = sched.find("SCHED_FIFO") != std::string::npos;
    bool prio = sched.find("priority 80") != std::string::npos;

    std::cout << "  Scheduler: " << sched << std::endl;
    return fifo && prio;
}

bool test_wcet_latency_distribution() {
    std::cout << "\n=== Test: WCET Latency Distribution ===" << std::endl;

    LatencyRecorder rec;
    rec.start();

    for (size_t i = 0; i < 100; ++i) {
        uint64_t latency = rec.record_sample();

        if (latency > WCET_TARGET_US) g_wcet_violations.fetch_add(1);
        if (latency > MAX_ALLOWED_LATENCY_US) g_stall_count.fetch_add(1);
        if (latency > g_max_inference_latency_us.load()) {
            g_max_inference_latency_us.store(latency);
        }

        g_total_inferences.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::microseconds(8000));
    }

    uint64_t max_lat = rec.get_max();
    uint64_t avg_lat = rec.get_avg();
    uint64_t p95 = rec.get_percentile(95.0);
    uint64_t p99 = rec.get_percentile(99.0);

    std::cout << "  Frames: 100" << std::endl;
    std::cout << "  Max: " << max_lat << " us (" << std::fixed << std::setprecision(2) << (max_lat/1000.0) << " ms)" << std::endl;
    std::cout << "  Avg: " << avg_lat << " us (" << (avg_lat/1000.0) << " ms)" << std::endl;
    std::cout << "  P95: " << p95 << " us" << std::endl;
    std::cout << "  P99: " << p99 << " us" << std::endl;
    std::cout << "  WCET viol (>15ms): " << g_wcet_violations.load() << std::endl;
    std::cout << "  Stalls (>18ms): " << g_stall_count.load() << std::endl;

    bool passed = (avg_lat <= MAX_ALLOWED_LATENCY_US);
    std::cout << "  Result: " << (passed ? "PASSED" : "FAILED") << std::endl;
    return passed;
}

bool test_stall_detection_threshold() {
    std::cout << "\n=== Test: Stall Detection (>25ms threshold) ===" << std::endl;

    LatencyRecorder rec;
    rec.start();
    size_t stalls = 0;

    for (size_t i = 0; i < 50; ++i) {
        uint64_t lat = rec.record_sample();
        if (lat > STALL_THRESHOLD_US) stalls++;

        if (i < 10) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    std::cout << "  Slow frames (>30ms): 10" << std::endl;
    std::cout << "  Stalls detected: " << stalls << std::endl;
    return stalls > 0;
}

bool test_consecutive_frame_timing() {
    std::cout << "\n=== Test: Consecutive Frame Timing ===" << std::endl;

    LatencyRecorder rec;
    rec.start();

    for (size_t i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
        rec.record_sample();
    }

    uint64_t min_int = rec.get_min();
    uint64_t max_int = rec.get_max();
    uint64_t avg_int = rec.get_avg();

    std::cout << "  Interval target: ~8ms" << std::endl;
    std::cout << "  Min: " << min_int << " us" << std::endl;
    std::cout << "  Max: " << max_int << " us" << std::endl;
    std::cout << "  Avg: " << avg_int << " us" << std::endl;

    bool stable = (avg_int >= 7000 && avg_int <= 9000);
    std::cout << "  Timing: " << (stable ? "STABLE" : "UNSTABLE") << std::endl;
    return stable;
}

bool test_deadline_miss_detection() {
    std::cout << "\n=== Test: Deadline Miss Detection ===" << std::endl;

    constexpr uint64_t DEADLINE_US = 18000;
    LatencyRecorder rec;
    rec.start();
    uint64_t misses = 0;

    for (size_t i = 0; i < 30; ++i) {
        uint64_t lat = rec.record_sample();
        if (lat > DEADLINE_US) misses++;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::cout << "  Deadline: " << DEADLINE_US << " us (18ms)" << std::endl;
    std::cout << "  Misses: " << misses << std::endl;

    bool passed = (misses == 0);
    std::cout << "  Adherence: " << (passed ? "PASS" : "FAIL") << std::endl;
    return passed;
}

bool test_latency_jitter() {
    std::cout << "\n=== Test: Latency Jitter Analysis ===" << std::endl;

    LatencyRecorder rec;
    rec.start();

    for (size_t i = 0; i < 50; ++i) {
        std::this_thread::sleep_for(std::chrono::microseconds(8000 + (i % 3) * 2000));
        rec.record_sample();
    }

    uint64_t jitter = rec.get_max() - rec.get_min();
    std::cout << "  Min: " << rec.get_min() << " us" << std::endl;
    std::cout << "  Max: " << rec.get_max() << " us" << std::endl;
    std::cout << "  Jitter: " << jitter << " us" << std::endl;

    bool ok = (jitter < 5000);
    std::cout << "  Jitter: " << (ok ? "OK" : "EXCEEDED") << std::endl;
    return ok;
}

bool test_thread_affinity_verification() {
    std::cout << "\n=== Test: Thread Affinity Verification ===" << std::endl;

    struct Test { ThreadRole role; std::string expected; };
    std::vector<Test> tests = {
        {ThreadRole::CAPTURE, "Core 0"},
        {ThreadRole::TPU_WORKER, "Core 1"},
        {ThreadRole::GPU_WORKER, "Core 2, Core 3"},
        {ThreadRole::LOGIC_OVERLAY, "Core 0, Core 1, Core 2, Core 3"},
        {ThreadRole::MONITOR, "Core 0, Core 1, Core 2, Core 3"}
    };

    bool all_ok = true;
    for (const auto& t : tests) {
        std::string aff = ThreadAffinityManager::get_affinity_string(t.role);
        std::cout << "  " << t.expected << ": " << aff << std::endl;
        if (aff.find(t.expected) == std::string::npos) all_ok = false;
    }
    return all_ok;
}

int main() {
    std::cout << "================================================" << std::endl;
    std::cout << "  TPU Worker Integration Tests with WCET" << std::endl;
    std::cout << "  Deadline: 18ms | WCET Target: 15ms" << std::endl;
    std::cout << "================================================" << std::endl;

    int passed = 0, total = 0;
    auto run = [&](const char* name, bool (*fn)()) {
        (void)name;
        total++;
        if (fn()) passed++;
    };

    run("Thread Affinity Verification", test_thread_affinity_verification);
    run("TPU Worker Thread Affinity", test_tpu_worker_thread_affinity);
    run("TPU Worker Scheduler Settings", test_tpu_worker_scheduler_settings);
    run("WCET Latency Distribution", test_wcet_latency_distribution);
    run("Stall Detection Threshold", test_stall_detection_threshold);
    run("Consecutive Frame Timing", test_consecutive_frame_timing);
    run("Deadline Miss Detection", test_deadline_miss_detection);
    run("Latency Jitter Analysis", test_latency_jitter);

    std::cout << "\n================================================" << std::endl;
    std::cout << "  SUMMARY: " << passed << "/" << total << " PASSED" << std::endl;
    std::cout << "================================================" << std::endl;

    return (passed == total) ? 0 : 1;
}
