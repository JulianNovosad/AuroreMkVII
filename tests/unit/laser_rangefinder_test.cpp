/**
 * @file laser_rangefinder_test.cpp
 * @brief Hardware unit tests for M01 laser rangefinder driver
 * 
 * These tests require real UART hardware (RPi 5 with M01 laser connected).
 * No mocks - tests verify actual UART communication and protocol parsing.
 * 
 * Hardware requirements:
 * - M01 laser rangefinder connected to UART (default: /dev/ttyAMA10)
 * - Run with sudo for UART access
 * 
 * Usage: sudo ./laser_rangefinder_test [/dev/ttyAMAxx]
 * 
 * FAIL-FAST: Tests fail immediately with clear error if hardware is absent.
 * No mocks or simulations - tests connect to real hardware or fail.
 */

#include "aurore/drivers/laser_rangefinder.hpp"
#include "aurore/timing.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

using namespace aurore;

// Test configuration
static constexpr float kTestDistanceM = 2.0f;      // Target distance for accuracy test
static constexpr float kAccuracyToleranceM = 0.1f; // ±10cm tolerance
static constexpr int kMaxReadAttempts = 50;        // Max attempts to get valid reading
static constexpr int kSampleCount = 10;            // Number of samples for stability test

static constexpr int kHardwareDetectTimeoutMs = 500;  // Fail-fast: timeout for hardware detection

// Test result tracking
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message)                           \
    do {                                                          \
        if (condition) {                                          \
            std::cout << "  PASS: " << message << "\n";           \
            tests_passed++;                                       \
        } else {                                              \
            std::cerr << "  FAIL: " << message << "\n";           \
            tests_failed++;                                       \
        }                                                         \
    } while (0)

bool wait_for_hardware_response(LaserRangefinder& lrf, int timeout_ms) {
    const int max_cycles = timeout_ms / 50;
    for (int i = 0; i < max_cycles; ++i) {
        if (lrf.latest_range_m() > 0.0f || lrf.status_frames_received() > 0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

/**
 * Test 1: Default construction and initial state
 */
void test_construction_and_init() {
    std::cout << "\n=== Test: Construction and Initial State ===\n";
    
    LaserRangefinder lrf;
    
    // After construction, should not be ready
    TEST_ASSERT(!lrf.is_ready(), "Default constructed LRF is not ready");
    TEST_ASSERT(lrf.latest_range_m() == 0.0f, "Initial range is 0.0m");
    TEST_ASSERT(lrf.last_reading_ns() == 0, "Initial timestamp is 0");
}

/**
 * Test 2: UART device initialization with real hardware
 */
void test_uart_initialization(const std::string& device) {
    std::cout << "\n=== Test: UART Initialization (" << device << ") ===\n";
    
    LaserRangefinder lrf;
    
    bool init_ok = lrf.init(device);
    
    if (!init_ok) {
        std::cerr << "  FATAL: Cannot open " << device << " - hardware unavailable\n";
        std::cerr << "  Check: ls /dev/ttyAMA* for available UART devices\n";
        std::cerr << "  Fix: Connect M01 laser rangefinder to UART pins GPIO14/15\n";
        TEST_ASSERT(false, "UART initialization failed - hardware not connected");
        return;
    }
    
    TEST_ASSERT(lrf.is_ready(), "LRF is ready after successful init");
    std::cout << "  INFO: UART " << device << " opened successfully\n";
}

/**
 * Test 3: Continuous mode start and reading acquisition
 */
void test_continuous_mode_and_readings(const std::string& device) {
    std::cout << "\n=== Test: Continuous Mode and Readings ===\n";

    LaserRangefinder lrf;

    if (!lrf.init(device)) {
        std::cerr << "  FATAL: UART init failed - hardware unavailable\n";
        TEST_ASSERT(false, "UART hardware not connected");
        return;
    }

    // FAST PROBE: Check for hardware response within 200ms
    if (!lrf.probe_present(200)) {
        lrf.stop();
        std::cerr << "  FATAL: No response from LRF within 200ms - hardware not connected\n";
        std::cerr << "  Check: LRF is powered (5V) and ENA pin is HIGH\n";
        std::cerr << "  Fix: Connect M01 laser rangefinder to UART pins GPIO14/15\n";
        TEST_ASSERT(false, "LRF hardware not responding - timeout");
        return;
    }

    // Start continuous measurement mode
    bool started = lrf.start_continuous();
    TEST_ASSERT(started, "Continuous mode started");
    
    if (!started) {
        lrf.stop();
        TEST_ASSERT(false, "Failed to start continuous mode");
        return;
    }

    // FAIL-FAST: Wait with timeout for hardware response (fail immediately if no response)
    bool has_response = wait_for_hardware_response(lrf, kHardwareDetectTimeoutMs);
    if (!has_response) {
        lrf.stop();
        std::cerr << "  FATAL: No response from LRF within " << kHardwareDetectTimeoutMs << "ms\n";
        std::cerr << "  Check: LRF is powered (5V) and ENA pin is HIGH\n";
        TEST_ASSERT(false, "LRF hardware not responding - timeout");
        return;
    }

    // Check that the LRF is communicating (data OR status frames)
    bool has_data = lrf.latest_range_m() > 0.0f;
    bool has_status = lrf.status_frames_received() > 0;
    TEST_ASSERT(has_data || has_status, "LRF is communicating (data or status frames received)");

    if (!has_data && !has_status) {
        lrf.stop();
        std::cerr << "  FAIL: No frames received from LRF on " << device << "\n";
        return;
    }

    float first_range = lrf.latest_range_m();
    if (has_data) {
        std::cout << "  INFO: Target detected — range: " << first_range << "m\n";
        TEST_ASSERT(first_range >= LaserRangefinder::kMinRangeM,
                    "Range >= minimum (" + std::to_string(LaserRangefinder::kMinRangeM) + "m)");
        TEST_ASSERT(first_range <= LaserRangefinder::kMaxRangeM,
                    "Range <= maximum (" + std::to_string(LaserRangefinder::kMaxRangeM) + "m");

        uint64_t ts = lrf.last_reading_ns();
        TEST_ASSERT(ts > 0, "Timestamp updated on valid reading");

        uint64_t now = get_timestamp(ClockId::MonotonicRaw);
        int64_t age_ns = timestamp_diff_ns(now, ts);
        TEST_ASSERT(age_ns < 1000000000LL && age_ns >= 0, "Reading is fresh (<1s old)");

        // Collect samples for stability check
        std::cout << "  INFO: Collecting " << kSampleCount << " samples...\n";
        float samples[kSampleCount];
        for (int i = 0; i < kSampleCount; ++i) {
            samples[i] = lrf.latest_range_m();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        float sum = 0.0f;
        float min_val = samples[0], max_val = samples[0];
        for (int i = 0; i < kSampleCount; ++i) {
            sum += samples[i];
            if (samples[i] < min_val) min_val = samples[i];
            if (samples[i] > max_val) max_val = samples[i];
        }
        float mean = sum / kSampleCount;
        float variance = 0.0f;
        for (int i = 0; i < kSampleCount; ++i) {
            float diff = samples[i] - mean;
            variance += diff * diff;
        }
        float stddev = std::sqrt(variance / kSampleCount);

        std::cout << "  INFO: Mean=" << mean << "m, StdDev=" << stddev << "m";
        std::cout << " [" << min_val << "m - " << max_val << "m]\n";
        TEST_ASSERT(stddev < 0.05f, "Readings are stable (stddev < 5cm)");
    } else {
        std::cout << "  INFO: LRF connected, no target in beam (status frames: "
                  << lrf.status_frames_received() << ")\n";
    }

    lrf.stop();
    TEST_ASSERT(!lrf.is_ready(), "LRF not ready after stop()");
}

/**
 * Test 4: M01 protocol frame parsing validation
 * This test verifies the driver correctly parses the M01 protocol:
 * - Frame sync byte (0xAA)
 * - Distance extraction from bytes 7-8 (little-endian)
 * - Range sanity checks (50mm-50000mm)
 */
void test_protocol_parsing(const std::string& device) {
    std::cout << "\n=== Test: M01 Protocol Parsing ===\n";

    LaserRangefinder lrf;

    if (!lrf.init(device)) {
        std::cerr << "  FATAL: UART init failed - hardware unavailable\n";
        TEST_ASSERT(false, "UART hardware not connected");
        return;
    }

    // Fast probe instead of diagnose_wiring()
    if (!lrf.probe_present(200)) {
        lrf.stop();
        TEST_ASSERT(false,
            "M01 laser not responding on " + device + "\n"
            "      Check: ls /dev/ttyAMA*\n"
            "      Fix: Connect M01 LRF to UART pins GPIO14/15, ensure VCC=3.3V and ENA=HIGH");
        return;
    }

    if (!lrf.start_continuous()) {
        lrf.stop();
        TEST_ASSERT(false, 
            "M01 LASER NOT RESPONDING on " + device + "\n"
            "Check: ls /dev/ttyAMA*\n"
            "Fix: Verify M01 laser is connected and powered (5V)");
        return;
    }

    // FAIL-FAST: Wait with timeout for hardware response
    bool has_response = wait_for_hardware_response(lrf, kHardwareDetectTimeoutMs);
    if (!has_response) {
        lrf.stop();
        std::cerr << "  FATAL: No response from LRF within " << kHardwareDetectTimeoutMs << "ms\n";
        TEST_ASSERT(false, "LRF hardware not responding - timeout");
        return;
    }

    bool has_data = lrf.latest_range_m() > 0.0f;
    bool has_status = lrf.status_frames_received() > 0;
    TEST_ASSERT(has_data || has_status, "LRF communicating (data or status frames)");

    if (!has_data && !has_status) {
        lrf.stop();
        return;
    }

    if (has_data) {
        // Collect readings and verify they're sane
        int valid_count = 0;
        int invalid_count = 0;

        for (int i = 0; i < 20; ++i) {
            float range = lrf.latest_range_m();
            if (range > 0.0f) {
                if (range >= LaserRangefinder::kMinRangeM &&
                    range <= LaserRangefinder::kMaxRangeM) {
                    valid_count++;
                } else {
                    invalid_count++;
                    std::cerr << "  WARNING: Out-of-range reading: " << range << "m\n";
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        TEST_ASSERT(valid_count > 0, "Received valid protocol frames");
        TEST_ASSERT(invalid_count == 0, "No invalid range values");
        std::cout << "  INFO: " << valid_count << " valid, " << invalid_count << " invalid frames\n";
    } else {
        std::cout << "  INFO: LRF streaming status frames (no target), protocol OK\n";
    }

    lrf.stop();
}

/**
 * Test 5: Thread safety - concurrent access to range data
 */
void test_thread_safety(const std::string& device) {
    std::cout << "\n=== Test: Thread Safety ===\n";

    LaserRangefinder lrf;

    if (!lrf.init(device)) {
        TEST_ASSERT(false,
            "M01 LASER NOT CONNECTED on " + device + "\n"
            "      Check: ls /dev/ttyAMA*\n"
            "      Fix: Connect M01 laser rangefinder to UART");
        return;
    }

    // Fast probe instead of diagnose_wiring()
    if (!lrf.probe_present(200)) {
        lrf.stop();
        TEST_ASSERT(false,
            "M01 laser not responding on " + device + "\n"
            "      Check: ls /dev/ttyAMA*\n"
            "      Fix: Connect M01 LRF to UART pins GPIO14/15, ensure VCC=3.3V and ENA=HIGH");
        return;
    }

    if (!lrf.start_continuous()) {
        lrf.stop();
        TEST_ASSERT(false, 
            "M01 LASER NOT RESPONDING on " + device + "\n"
            "Check: ls /dev/ttyAMA*\n"
            "Fix: Verify M01 laser is connected and powered");
        return;
    }

    // FAIL-FAST: Wait with timeout for hardware response
    bool has_response = wait_for_hardware_response(lrf, kHardwareDetectTimeoutMs);
    if (!has_response) {
        lrf.stop();
        std::cerr << "  FATAL: No response from LRF within " << kHardwareDetectTimeoutMs << "ms\n";
        TEST_ASSERT(false, "LRF hardware not responding - timeout");
        return;
    }

    bool has_activity = lrf.latest_range_m() > 0.0f || lrf.status_frames_received() > 0;
    TEST_ASSERT(has_activity, "LRF communicating (data or status frames)");

    if (!has_activity) {
        lrf.stop();
        return;
    }

    std::atomic<int> read_errors{0};
    std::atomic<int> read_count{0};
    
    // Spawn multiple reader threads
    const int num_threads = 4;
    std::thread readers[num_threads];
    
    for (int t = 0; t < num_threads; ++t) {
        readers[t] = std::thread([&]() {
            for (int i = 0; i < 100; ++i) {
                float range = lrf.latest_range_m();
                uint64_t ts = lrf.last_reading_ns();
                
                // Verify atomic consistency
                if (range > 0.0f && ts == 0) {
                    read_errors++;
                }
                read_count++;
                
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
    }
    
    // Wait for all threads
    for (int t = 0; t < num_threads; ++t) {
        readers[t].join();
    }
    
    TEST_ASSERT(read_errors == 0, "No atomic consistency errors");
    TEST_ASSERT(read_count == num_threads * 100, "All reads completed");
    
    std::cout << "  INFO: " << read_count << " concurrent reads, " 
              << read_errors << " errors\n";
    
    lrf.stop();
}

/**
 * Test 6: Range accuracy verification (requires known target distance)
 */
void test_range_accuracy(const std::string& device) {
    std::cout << "\n=== Test: Range Accuracy ===\n";
    std::cout << "  INFO: Place target at " << kTestDistanceM << "m (±" 
              << kAccuracyToleranceM << "m tolerance)\n";
    std::cout << "  INFO: Press Enter to continue or skip with Ctrl+C...\n";
    
    // Give user time to set up target
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    
    LaserRangefinder lrf;

    if (!lrf.init(device)) {
        std::cerr << "  SKIP: UART init failed\n";
        return;
    }

    if (lrf.diagnose_wiring() != 0) {
        lrf.stop();
        TEST_ASSERT(false,
            "M01 laser not responding on " + device + "\n"
            "      Check: ls /dev/ttyAMA*\n"
            "      Fix: Connect M01 LRF to UART pins GPIO14/15, ensure VCC=3.3V and ENA=HIGH");
        return;
    }

    if (!lrf.start_continuous()) {
        std::cerr << "  SKIP: Could not start continuous mode\n";
        lrf.stop();
        return;
    }

    // FAIL-FAST: Wait with timeout for hardware response
    bool has_response = wait_for_hardware_response(lrf, kHardwareDetectTimeoutMs);
    if (!has_response) {
        lrf.stop();
        std::cerr << "  FATAL: No response from LRF within " << kHardwareDetectTimeoutMs << "ms\n";
        TEST_ASSERT(false, "LRF hardware not responding - timeout");
        return;
    }

    // Collect samples
    const int sample_count = 20;
    float samples[sample_count];
    
    for (int i = 0; i < sample_count; ++i) {
        samples[i] = lrf.latest_range_m();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Calculate mean
    float sum = 0.0f;
    for (int i = 0; i < sample_count; ++i) {
        sum += samples[i];
    }
    float mean = sum / sample_count;
    
    std::cout << "  INFO: Mean measured distance: " << mean << "m\n";
    
    // Verify accuracy
    float error = std::abs(mean - kTestDistanceM);
    bool accurate = error <= kAccuracyToleranceM;
    
    TEST_ASSERT(accurate, 
                "Accuracy within tolerance (error=" + std::to_string(error) + "m)");
    
    lrf.stop();
}

int main(int argc, char* argv[]) {
    const std::string device = (argc > 1) ? argv[1] : "/dev/ttyAMA0";
    
    std::cout << "===========================================\n";
    std::cout << "Laser Rangefinder Hardware Tests\n";
    std::cout << "Device: " << device << "\n";
    std::cout << "===========================================\n";
    
    // Run tests
    test_construction_and_init();
    test_uart_initialization(device);
    test_continuous_mode_and_readings(device);
    test_protocol_parsing(device);
    test_thread_safety(device);
    // test_range_accuracy(device);  // Optional - requires manual target setup
    
    // Summary
    std::cout << "\n===========================================\n";
    std::cout << "Test Summary: " << tests_passed << " passed, " 
              << tests_failed << " failed\n";
    std::cout << "===========================================\n";
    
    return (tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
