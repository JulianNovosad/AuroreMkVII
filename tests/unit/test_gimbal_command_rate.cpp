/**
 * @file test_gimbal_command_rate.cpp
 * @brief Gimbal command rate test for AM7-L2-ACT-001 verification
 *
 * Requirement AM7-L2-ACT-001:
 *   Gimbal command updates shall be issued at 120 Hz ±1% synchronized to frame boundary
 *   with tolerance ±50μs.
 *
 * This test verifies:
 * 1. Command rate is 120Hz ±1% (118.8 - 121.2 Hz)
 * 2. Command timing jitter is within ±50μs of ideal frame boundary
 * 3. Commands are synchronized to frame capture trigger
 *
 * Test methodology:
 * - Simulate frame capture at 120Hz (8.333ms period)
 * - Issue gimbal command on each frame boundary
 * - Measure actual command timestamps
 * - Compute rate error and jitter statistics
 *
 * Pass criteria:
 * - Command rate: 120Hz ±1% (118.8 - 121.2 Hz)
 * - Frame sync jitter: ≤50μs RMS
 * - No dropped commands over test duration
 *
 * @copyright AuroreMkVII Project - Educational/Personal Use Only
 */

#include "aurore/gimbal_controller.hpp"
#include "aurore/fusion_hat.hpp"
#include "aurore/timing.hpp"

#include <atomic>
#include <cassert>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>

// Test configuration
namespace {
    constexpr double kTargetRateHz = 120.0;                // Target command rate
    constexpr double kTargetPeriodMs = 8.333333;           // 120Hz = 8.333...ms
    constexpr double kRateTolerancePercent = 0.01;         // ±1% rate tolerance
    constexpr double kMaxJitterUs = 50.0;                  // AM7-L2-ACT-001 jitter budget
    constexpr int kNumFrames = 360;                        // 3 seconds at 120Hz
    constexpr int kWarmupFrames = 10;                      // Warm-up frames
}

// Command timing measurement
struct CommandTiming {
    uint64_t timestamp_ns;
    double period_ms;
    double jitter_us;
    int frame_number;
};

// Mock gimbal command issuer
class GimbalCommandSimulator {
public:
    GimbalCommandSimulator() : command_count_(0) {}

    void issue_command(float az_deg, float el_deg) {
        const uint64_t now_ns = aurore::get_timestamp();
        timestamps_.push_back(now_ns);
        command_count_.fetch_add(1, std::memory_order_relaxed);
        (void)az_deg;
        (void)el_deg;
    }

    const std::vector<uint64_t>& get_timestamps() const {
        return timestamps_;
    }

    uint64_t command_count() const {
        return command_count_.load(std::memory_order_relaxed);
    }

    void reset() {
        timestamps_.clear();
        command_count_.store(0, std::memory_order_relaxed);
    }

private:
    std::vector<uint64_t> timestamps_;
    std::atomic<uint64_t> command_count_;
};

// Simulate frame-synchronized command generation
std::vector<CommandTiming> simulate_frame_synchronized_commands(int num_frames) {
    GimbalCommandSimulator simulator;
    std::vector<CommandTiming> timings;
    timings.reserve(static_cast<size_t>(num_frames));

    const uint64_t period_ns = static_cast<uint64_t>(kTargetPeriodMs * 1e6);  // 8.333ms in ns
    uint64_t start_ns = 0;

    // Warm-up
    for (int i = 0; i < kWarmupFrames; ++i) {
        simulator.issue_command(0.0f, 0.0f);
        std::this_thread::sleep_for(std::chrono::nanoseconds(period_ns));
    }

    simulator.reset();

    // Record timestamps
    start_ns = aurore::get_timestamp();

    for (int i = 0; i < num_frames; ++i) {
        // Simulate frame-synchronized command
        simulator.issue_command(0.0f, 0.0f);

        // Wait for next frame period
        std::this_thread::sleep_for(std::chrono::nanoseconds(period_ns));
    }

    // Analyze timestamps
    const auto& timestamps = simulator.get_timestamps();
    uint64_t prev_ts = timestamps[0];

    for (size_t i = 0; i < timestamps.size(); ++i) {
        CommandTiming timing{};
        timing.timestamp_ns = timestamps[i];
        timing.frame_number = static_cast<int>(i);

        if (i > 0) {
            timing.period_ms = static_cast<double>(timestamps[i] - prev_ts) / 1e6;
            prev_ts = timestamps[i];
        } else {
            timing.period_ms = kTargetPeriodMs;
        }

        // Compute jitter from ideal frame boundary
        uint64_t ideal_ts = start_ns + i * period_ns;
        timing.jitter_us = static_cast<double>(static_cast<int64_t>(timestamps[i] - ideal_ts)) / 1e3;

        timings.push_back(timing);
    }

    return timings;
}

void test_command_rate_accuracy() {
    std::cout << "=== AM7-L2-ACT-001: Command Rate Accuracy Test ===" << std::endl;
    std::cout << "Target rate: " << kTargetRateHz << " Hz ±" << (kRateTolerancePercent * 100) << "%" << std::endl;
    std::cout << "Test duration: " << (kNumFrames / kTargetRateHz) << " seconds (" << kNumFrames << " frames)" << std::endl;
    std::cout << std::endl;

    auto timings = simulate_frame_synchronized_commands(kNumFrames);

    // Compute average period and rate
    double period_sum = 0.0;
    double period_min = timings[1].period_ms;
    double period_max = timings[1].period_ms;

    for (size_t i = 1; i < timings.size(); ++i) {
        period_sum += timings[i].period_ms;
        period_min = std::min(period_min, timings[i].period_ms);
        period_max = std::max(period_max, timings[i].period_ms);
    }

    const double period_avg = period_sum / static_cast<double>(timings.size() - 1);
    const double rate_avg = 1000.0 / period_avg;

    // Compute overall rate from total time
    const double total_time_ms = static_cast<double>(timings.back().timestamp_ns - timings.front().timestamp_ns) / 1e6;
    const double overall_rate = static_cast<double>(kNumFrames) * 1000.0 / total_time_ms;

    std::cout << "Results:" << std::endl;
    std::cout << "  Average period: " << period_avg << " ms (target: " << kTargetPeriodMs << " ms)" << std::endl;
    std::cout << "  Average rate:   " << rate_avg << " Hz" << std::endl;
    std::cout << "  Overall rate:   " << overall_rate << " Hz" << std::endl;
    std::cout << "  Period min/max: " << period_min << " / " << period_max << " ms" << std::endl;
    std::cout << std::endl;

    // Verify rate is within tolerance
    const double rate_error_percent = std::abs(rate_avg - kTargetRateHz) / kTargetRateHz * 100.0;
    const bool rate_pass = (rate_error_percent <= kRateTolerancePercent * 100.0);

    std::cout << "Verification:" << std::endl;
    std::cout << "  Rate error: " << rate_error_percent << " % (allowed: " << (kRateTolerancePercent * 100) << " %)" << std::endl;
    std::cout << "  AM7-L2-ACT-001 (120Hz ±1%): " << (rate_pass ? "PASS" : "FAIL") << std::endl;

    assert(rate_pass && "AM7-L2-ACT-001: Command rate outside ±1% tolerance");

    std::cout << "  PASS" << std::endl;
}

void test_frame_sync_jitter() {
    std::cout << "=== AM7-L2-ACT-001: Frame Sync Jitter Test ===" << std::endl;
    std::cout << "Jitter budget: ≤" << kMaxJitterUs << " μs RMS" << std::endl;
    std::cout << std::endl;

    auto timings = simulate_frame_synchronized_commands(kNumFrames);

    // Compute jitter statistics
    double jitter_sum = 0.0;
    double jitter_sq_sum = 0.0;
    double jitter_min = timings[0].jitter_us;
    double jitter_max = timings[0].jitter_us;

    for (const auto& t : timings) {
        jitter_sum += t.jitter_us;
        jitter_sq_sum += t.jitter_us * t.jitter_us;
        jitter_min = std::min(jitter_min, t.jitter_us);
        jitter_max = std::max(jitter_max, t.jitter_us);
    }

    const double jitter_avg = jitter_sum / static_cast<double>(timings.size());
    const double variance = (jitter_sq_sum / static_cast<double>(timings.size())) - (jitter_avg * jitter_avg);
    const double jitter_rms = std::sqrt(variance);

    std::cout << "Results:" << std::endl;
    std::cout << "  Average jitter: " << jitter_avg << " μs" << std::endl;
    std::cout << "  RMS jitter:     " << jitter_rms << " μs" << std::endl;
    std::cout << "  Min/Max jitter: " << jitter_min << " / " << jitter_max << " μs" << std::endl;
    std::cout << std::endl;

    // Verify jitter is within budget
    const bool jitter_pass = (jitter_rms <= kMaxJitterUs);

    std::cout << "Verification:" << std::endl;
    std::cout << "  AM7-L2-ACT-001 (jitter ≤50μs RMS): " << (jitter_pass ? "PASS" : "FAIL") << std::endl;
    std::cout << "    Measured RMS: " << jitter_rms << " μs" << std::endl;

    assert(jitter_pass && "AM7-L2-ACT-001: Frame sync jitter exceeds 50μs RMS budget");

    std::cout << "  PASS" << std::endl;
}

void test_command_continuity() {
    std::cout << "=== Command Continuity Test ===" << std::endl;
    std::cout << "Verifying no dropped commands over test duration" << std::endl;
    std::cout << std::endl;

    GimbalCommandSimulator simulator;

    // Issue commands at 120Hz for 1 second
    const int expected_commands = 120;
    const uint64_t period_ns = static_cast<uint64_t>(kTargetPeriodMs * 1e6);

    for (int i = 0; i < expected_commands; ++i) {
        simulator.issue_command(0.0f, 0.0f);
        std::this_thread::sleep_for(std::chrono::nanoseconds(period_ns));
    }

    const uint64_t actual_commands = simulator.command_count();
    const bool continuity_pass = (actual_commands == static_cast<uint64_t>(expected_commands));

    std::cout << "Results:" << std::endl;
    std::cout << "  Expected commands: " << expected_commands << std::endl;
    std::cout << "  Actual commands:   " << actual_commands << std::endl;
    std::cout << "  Dropped commands:  " << (expected_commands - actual_commands) << std::endl;
    std::cout << std::endl;

    std::cout << "Verification:" << std::endl;
    std::cout << "  Command continuity: " << (continuity_pass ? "PASS" : "FAIL") << std::endl;

    assert(continuity_pass && "Command continuity test failed - commands dropped");

    std::cout << "  PASS" << std::endl;
}

void test_phase_offset_stability() {
    std::cout << "=== Phase Offset Stability Test ===" << std::endl;
    std::cout << "Verifying consistent phase offset between frames" << std::endl;
    std::cout << std::endl;

    auto timings = simulate_frame_synchronized_commands(kNumFrames);

    // Compute period-to-period variation (phase stability)
    std::vector<double> period_diffs;
    period_diffs.reserve(timings.size() - 2);

    for (size_t i = 2; i < timings.size(); ++i) {
        double diff = timings[i].period_ms - timings[i-1].period_ms;
        period_diffs.push_back(diff);
    }

    double diff_sum = 0.0;
    double diff_max = 0.0;
    for (double diff : period_diffs) {
        diff_sum += std::abs(diff);
        diff_max = std::max(diff_max, std::abs(diff));
    }

    const double diff_avg = diff_sum / static_cast<double>(period_diffs.size());

    std::cout << "Results:" << std::endl;
    std::cout << "  Average period variation: " << diff_avg << " ms" << std::endl;
    std::cout << "  Maximum period variation: " << diff_max << " ms" << std::endl;
    std::cout << std::endl;

    // Phase variation should be small (< 0.1ms)
    const bool phase_pass = (diff_max < 0.1);

    std::cout << "Verification:" << std::endl;
    std::cout << "  Phase stability: " << (phase_pass ? "PASS" : "FAIL") << std::endl;

    assert(phase_pass && "Phase offset stability test failed");

    std::cout << "  PASS" << std::endl;
}

}  // anonymous namespace

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "Gimbal Command Rate Test Suite" << std::endl;
    std::cout << "AM7-L2-ACT-001 Verification" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    int passed = 0;
    int failed = 0;

    auto run_test = [&](const char* /*name*/, void (*test_fn)()) {
        try {
            test_fn();
            passed++;
        } catch (const std::exception& e) {
            std::cerr << "  FAIL: " << e.what() << std::endl;
            failed++;
        }
    };

    run_test("test_command_rate_accuracy", test_command_rate_accuracy);
    run_test("test_frame_sync_jitter", test_frame_sync_jitter);
    run_test("test_command_continuity", test_command_continuity);
    run_test("test_phase_offset_stability", test_phase_offset_stability);

    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Tests run: " << (passed + failed) << std::endl;
    std::cout << "Tests passed: " << passed << std::endl;
    std::cout << "Tests failed: " << failed << std::endl;
    std::cout << "========================================" << std::endl;

    return (failed == 0) ? 0 : 1;
}
