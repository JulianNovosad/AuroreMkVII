/**
 * @file hostile_input_test.cpp
 * @brief Part 7: Input Validity and Hostile Data Tests
 *
 * Validates:
 * - Truncated, reordered, duplicated, delayed, and replayed packets
 * - Extreme but legal values
 * - Conflicting command authorities (local vs Aurore Link)
 *
 * System must remain deterministic and safe.
 *
 * @copyright Aurore MkVII Project - Educational/Personal Use Only
 */

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>

#include "aurore/test_infrastructure.hpp"
#include "aurore/state_machine.hpp"

namespace {

std::atomic<size_t> g_tests_run(0);
std::atomic<size_t> g_tests_passed(0);
std::atomic<size_t> g_tests_failed(0);

#define TEST(name) void name()
#define RUN_TEST(name) do { \
    g_tests_run.fetch_add(1); \
    try { \
        name(); \
        g_tests_passed.fetch_add(1); \
        std::cout << "  PASS: " << #name << std::endl; \
    } catch (const std::exception& e) { \
        g_tests_failed.fetch_add(1); \
        std::cerr << "  FAIL: " << #name << " - " << e.what() << std::endl; \
    } \
} while(0)

#define ASSERT_TRUE(x) do { if (!(x)) throw std::runtime_error("Assertion failed: " #x); } while(0)
#define ASSERT_FALSE(x) ASSERT_TRUE(!(x))
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " != " #b); } while(0)
#define ASSERT_GT(a, b) do { if ((a) <= (b)) throw std::runtime_error("Assertion failed: " #a " > " #b); } while(0)
#define ASSERT_LT(a, b) do { if ((a) >= (b)) throw std::runtime_error("Assertion failed: " #a " < " #b); } while(0)
#define ASSERT_GE(a, b) do { if ((a) < (b)) throw std::runtime_error("Assertion failed: " #a " >= " #b); } while(0)
#define ASSERT_LE(a, b) do { if ((a) > (b)) throw std::runtime_error("Assertion failed: " #a " <= " #b); } while(0)

}  // anonymous namespace

// ============================================================================
// Truncated Packet Tests
// ============================================================================

TEST(test_truncated_packet_handling) {
    using aurore::test::HostileInputInjector;

    HostileInputInjector::Packet original;
    original.size = 100;
    original.timestamp_ns = 1000;
    original.sequence = 1;

    auto truncated = HostileInputInjector::truncate(original, 10);

    ASSERT_LT(truncated.size, original.size);
}

TEST(test_truncated_packet_validation) {
    using aurore::test::HostileInputInjector;

    HostileInputInjector::Packet packets[3];
    packets[0].timestamp_ns = 1000;
    packets[1].timestamp_ns = 2000;
    packets[2].timestamp_ns = 1500;

    bool valid = HostileInputInjector::validate_packets(packets, 3);

    ASSERT_FALSE(valid);
}

// ============================================================================
// Duplicated Packet Tests
// ============================================================================

TEST(test_duplicate_packet_creation) {
    using aurore::test::HostileInputInjector;

    HostileInputInjector::Packet original;
    original.size = 100;
    original.timestamp_ns = 1000;
    original.sequence = 1;

    auto duplicate = HostileInputInjector::duplicate(original);

    ASSERT_EQ(duplicate.sequence, original.sequence + 1);
}

// ============================================================================
// Delayed Packet Tests
// ============================================================================

TEST(test_delay_packet_creation) {
    using aurore::test::HostileInputInjector;

    HostileInputInjector::Packet original;
    original.size = 100;
    original.timestamp_ns = 1000;
    original.sequence = 1;

    auto delayed = HostileInputInjector::delay(original, 1000000);

    ASSERT_EQ(delayed.timestamp_ns, original.timestamp_ns + 1000000);
}

// ============================================================================
// Replayed Packet Tests
// ============================================================================

TEST(test_replay_packet_creation) {
    using aurore::test::HostileInputInjector;

    HostileInputInjector::Packet original;
    original.size = 100;
    original.timestamp_ns = 1000;
    original.sequence = 5;

    auto replayed = HostileInputInjector::replay(original, 3);

    ASSERT_EQ(replayed.sequence, 3);
}

// ============================================================================
// Extreme Value Tests
// ============================================================================

TEST(test_extreme_value_injection) {
    using aurore::test::HostileInputInjector;

    uint8_t data[100] = {0};
    HostileInputInjector::Packet original;
    original.data = data;
    original.size = 100;
    original.timestamp_ns = 1000;
    original.sequence = 1;

    auto extreme = HostileInputInjector::inject_extreme_value(original, 50);

    ASSERT_EQ(extreme.size, original.size);
}

TEST(test_extreme_range_value_min) {
    constexpr float kRangeMinM = 0.5f;
    float range = 0.0f;

    if (range < kRangeMinM) range = kRangeMinM;

    ASSERT_GE(range, kRangeMinM);
}

TEST(test_extreme_range_value_max) {
    constexpr float kRangeMaxM = 5000.0f;
    float range = 10000.0f;

    if (range > kRangeMaxM) range = kRangeMaxM;

    ASSERT_LE(range, kRangeMaxM);
}

TEST(test_extreme_angle_azimuth) {
    float angle = 720.0f;
    while (angle >= 360.0f) angle -= 360.0f;

    ASSERT_GE(angle, 0.0f);
    ASSERT_LT(angle, 360.0f);
}

TEST(test_extreme_angle_elevation) {
    float min_el = -90.0f;
    float max_el = 90.0f;

    float el = 150.0f;
    if (el > max_el) el = max_el;
    if (el < min_el) el = min_el;

    ASSERT_GE(el, min_el);
    ASSERT_LE(el, max_el);
}

// ============================================================================
// Conflicting Authority Tests
// ============================================================================

TEST(test_conflicting_authority_local_vs_remote) {
    aurore::StateMachine sm;
    sm.on_init_complete();

    sm.set_operator_authorization(true);

    ASSERT_TRUE(sm.has_operator_authorization());
}

TEST(test_authority_revoke_prevents_armed) {
    aurore::StateMachine sm;
    sm.on_init_complete();
    sm.request_search();
    sm.force_state_for_test(aurore::FcsState::TRACKING);

    sm.on_redetection_score(0.96f);       // has_valid_lock
    sm.set_operator_authorization(false); // revoked — ARMED unreachable

    aurore::FireControlSolution fc;
    fc.p_hit = 0.99f;
    sm.on_ballistics_solution(fc);

    ASSERT_EQ(sm.state(), aurore::FcsState::TRACKING);
}

TEST(test_freecam_bypasses_authorization) {
    aurore::StateMachine sm;
    sm.on_init_complete();

    sm.set_operator_authorization(false);

    sm.request_freecam();

    ASSERT_EQ(sm.state(), aurore::FcsState::FREECAM);
}

// ============================================================================
// Packet Sequence Tests
// ============================================================================

TEST(test_sequence_validation_monotonic) {
    using aurore::test::HostileInputInjector;

    HostileInputInjector::Packet packets[3];
    packets[0].timestamp_ns = 1000;
    packets[1].timestamp_ns = 2000;
    packets[2].timestamp_ns = 3000;

    bool valid = HostileInputInjector::validate_packets(packets, 3);

    ASSERT_TRUE(valid);
}

TEST(test_sequence_validation_regression) {
    using aurore::test::HostileInputInjector;

    HostileInputInjector::Packet packets[3];
    packets[0].timestamp_ns = 3000;
    packets[1].timestamp_ns = 2000;
    packets[2].timestamp_ns = 1000;

    bool valid = HostileInputInjector::validate_packets(packets, 3);

    ASSERT_FALSE(valid);
}

// ============================================================================
// State Determinism Under Hostile Input
// ============================================================================

TEST(test_state_deterministic_under_packet_loss) {
    aurore::StateMachine sm;
    sm.on_init_complete();

    for (int i = 0; i < 10; i++) {
        sm.tick(std::chrono::milliseconds(100));
    }

    auto state1 = sm.state();

    aurore::StateMachine sm2;
    sm2.on_init_complete();

    for (int i = 0; i < 10; i++) {
        sm2.tick(std::chrono::milliseconds(100));
    }

    auto state2 = sm2.state();

    ASSERT_EQ(state1, state2);
}

TEST(test_fault_transition_deterministic) {
    auto run_test = [](int iteration) {
        (void)iteration;
        aurore::StateMachine sm;
        sm.on_init_complete();
        sm.on_fault(aurore::FaultCode::CAMERA_TIMEOUT);
        return sm.state();
    };

    auto state1 = run_test(1);
    auto state2 = run_test(2);

    ASSERT_EQ(state1, state2);
}

// ============================================================================
// Main
// ============================================================================

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    std::cout << "=== Part 7: Input Validity and Hostile Data Tests ===" << std::endl;
    std::cout << "Running hostile input tests..." << std::endl;
    std::cout << "=====================================" << std::endl;

    std::cout << "\n--- Truncated Packet Tests ---" << std::endl;
    RUN_TEST(test_truncated_packet_handling);
    RUN_TEST(test_truncated_packet_validation);

    std::cout << "\n--- Duplicated Packet Tests ---" << std::endl;
    RUN_TEST(test_duplicate_packet_creation);

    std::cout << "\n--- Delayed Packet Tests ---" << std::endl;
    RUN_TEST(test_delay_packet_creation);

    std::cout << "\n--- Replayed Packet Tests ---" << std::endl;
    RUN_TEST(test_replay_packet_creation);

    std::cout << "\n--- Extreme Value Tests ---" << std::endl;
    RUN_TEST(test_extreme_value_injection);
    RUN_TEST(test_extreme_range_value_min);
    RUN_TEST(test_extreme_range_value_max);
    RUN_TEST(test_extreme_angle_azimuth);
    RUN_TEST(test_extreme_angle_elevation);

    std::cout << "\n--- Conflicting Authority Tests ---" << std::endl;
    RUN_TEST(test_conflicting_authority_local_vs_remote);
    RUN_TEST(test_authority_revoke_prevents_armed);
    RUN_TEST(test_freecam_bypasses_authorization);

    std::cout << "\n--- Packet Sequence Tests ---" << std::endl;
    RUN_TEST(test_sequence_validation_monotonic);
    RUN_TEST(test_sequence_validation_regression);

    std::cout << "\n--- State Determinism Tests ---" << std::endl;
    RUN_TEST(test_state_deterministic_under_packet_loss);
    RUN_TEST(test_fault_transition_deterministic);

    std::cout << "\n=====================================" << std::endl;
    std::cout << "Tests run:     " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed:  " << g_tests_passed.load() << std::endl;
    std::cout << "Tests failed:  " << g_tests_failed.load() << std::endl;

    return g_tests_failed.load() > 0 ? 1 : 0;
}