/**
 * @file telemetry_writer_test.cpp
 * @brief Unit tests for TelemetryWriter async logging module
 *
 * Tests lifecycle, frame logging, backpressure drop policy, and queue stats.
 */

#include "aurore/telemetry_writer.hpp"
#include "aurore/telemetry_types.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <thread>
#include <chrono>

namespace {

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        std::cerr << "FAIL [" << __func__ << " line " << __LINE__ << "]: " \
                  << #expr << " is false\n"; \
        return false; \
    } \
} while (0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

static bool run_test(const char* name, bool (*fn)()) {
    ++g_tests_run;
    std::cout << "[ RUN ] " << name << "\n";
    bool ok = fn();
    if (ok) {
        ++g_tests_passed;
        std::cout << "[ OK  ] " << name << "\n";
    } else {
        std::cout << "[FAIL ] " << name << "\n";
    }
    return ok;
}

// Ensure the log directory exists before tests that write to it.
static void ensure_log_dir() {
    std::filesystem::create_directories("/tmp/aurore_test_logs");
}

// ---------------------------------------------------------------------------
// Test 1 — writer starts and stops cleanly
// ---------------------------------------------------------------------------
static bool test_writer_starts_and_stops() {
    ensure_log_dir();

    aurore::TelemetryWriter writer;

    aurore::TelemetryConfig cfg;
    cfg.log_dir = "/tmp/aurore_test_logs";
    cfg.enable_json = false;

    ASSERT_TRUE(writer.start(cfg));
    ASSERT_TRUE(writer.is_running());

    writer.stop();
    ASSERT_FALSE(writer.is_running());

    return true;
}

// ---------------------------------------------------------------------------
// Test 2 — log_frame increments the written counter
// ---------------------------------------------------------------------------
static bool test_log_frame_increments_counter() {
    ensure_log_dir();

    aurore::TelemetryWriter writer;

    aurore::TelemetryConfig cfg;
    cfg.log_dir = "/tmp/aurore_test_logs";
    cfg.enable_json = false;

    ASSERT_TRUE(writer.start(cfg));

    aurore::DetectionData det{};
    aurore::TrackData track{};
    aurore::ActuationData act{};
    aurore::SystemHealthData health{};

    writer.log_frame(det, track, act, health);
    writer.log_frame(det, track, act, health);
    writer.log_frame(det, track, act, health);

    // stop() drains the queue before returning
    writer.stop();

    ASSERT_TRUE(writer.get_entries_written() == 3);

    return true;
}

// ---------------------------------------------------------------------------
// Test 3 — backpressure kDropNewest drops entries when queue is full
// ---------------------------------------------------------------------------
static bool test_backpressure_drop_policy() {
    ensure_log_dir();

    aurore::TelemetryWriter writer;

    aurore::TelemetryConfig cfg;
    cfg.log_dir = "/tmp/aurore_test_logs";
    cfg.enable_json = false;
    // Queue of 1 with kDropNewest: first push fills the queue (depth 1 == max),
    // second push is unconditionally dropped by enqueue_entry() before the
    // writer thread can drain anything (the drop path returns immediately under
    // the caller's lock). This guarantees >= 1 drop regardless of writer speed.
    cfg.max_queue_size = 1;
    cfg.backpressure_policy = aurore::BackpressurePolicy::kDropNewest;

    ASSERT_TRUE(writer.start(cfg));

    aurore::DetectionData det{};
    aurore::TrackData track{};
    aurore::ActuationData act{};
    aurore::SystemHealthData health{};

    writer.log_frame(det, track, act, health);  // fills queue (depth 1 == max)
    writer.log_frame(det, track, act, health);  // guaranteed drop

    writer.stop();

    ASSERT_TRUE(writer.get_entries_dropped() >= 1);

    return true;
}

// ---------------------------------------------------------------------------
// Test 4 — get_queue_stats() reports max_depth == cfg.max_queue_size
// ---------------------------------------------------------------------------
static bool test_queue_stats_accessible() {
    ensure_log_dir();

    aurore::TelemetryWriter writer;

    aurore::TelemetryConfig cfg;
    cfg.log_dir = "/tmp/aurore_test_logs";
    cfg.enable_json = false;
    cfg.max_queue_size = 42;

    ASSERT_TRUE(writer.start(cfg));

    aurore::TelemetryQueueStats stats = writer.get_queue_stats();
    ASSERT_TRUE(stats.max_depth == cfg.max_queue_size);

    writer.stop();

    return true;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::cout << "=== TelemetryWriterTest ===\n";

    run_test("test_writer_starts_and_stops",         test_writer_starts_and_stops);
    run_test("test_log_frame_increments_counter",    test_log_frame_increments_counter);
    run_test("test_backpressure_drop_policy",        test_backpressure_drop_policy);
    run_test("test_queue_stats_accessible",          test_queue_stats_accessible);

    std::cout << "\nResults: " << g_tests_passed << "/" << g_tests_run << " passed\n";

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
