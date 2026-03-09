/**
 * @file sequence_validation_test.cpp
 * @brief Unit tests for RFC 1982 sequence number validation
 *
 * Tests cover:
 * - RFC 1982 wrap-aware sequence comparison
 * - Sequence gap detection
 * - Replay attack prevention
 * - Edge cases (wrap-around, large gaps)
 */

#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

#include "aurore/security.hpp"

namespace {

// Test counters
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
    } \
    catch (const std::exception& e) { \
        g_tests_failed.fetch_add(1); \
        std::cerr << "  FAIL: " << #name << " - " << e.what() << std::endl; \
    } \
} while(0)

#define ASSERT_TRUE(x) do { if (!(x)) throw std::runtime_error("Assertion failed: " #x); } while(0)
#define ASSERT_FALSE(x) ASSERT_TRUE(!(x))
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " != " #b); } while(0)

}  // anonymous namespace

// ============================================================================
// RFC 1982 Sequence Number Verification Tests
// ============================================================================

TEST(test_sequence_normal_order) {
    // Normal case: current > expected
    ASSERT_TRUE(aurore::security::verify_sequence_number(10, 5));
    ASSERT_TRUE(aurore::security::verify_sequence_number(100, 1));
    ASSERT_TRUE(aurore::security::verify_sequence_number(1000, 500));
}

TEST(test_sequence_exact_match) {
    // Exact match should be valid (current == expected)
    ASSERT_TRUE(aurore::security::verify_sequence_number(10, 10));
    ASSERT_TRUE(aurore::security::verify_sequence_number(0, 0));
    ASSERT_TRUE(aurore::security::verify_sequence_number(100, 100));
}

TEST(test_sequence_replay_detection) {
    // Replay attack: current < expected (old message)
    ASSERT_FALSE(aurore::security::verify_sequence_number(5, 10));
    ASSERT_FALSE(aurore::security::verify_sequence_number(1, 100));
    ASSERT_FALSE(aurore::security::verify_sequence_number(500, 1000));
}

TEST(test_sequence_wrap_around_small) {
    // Wrap-around: expected near max, current wrapped to small value
    // This should be VALID (forward progress with wrap)
    uint32_t max_uint32 = 0xFFFFFFFFu;
    
    // Expected = max, current = 0 (valid wrap)
    ASSERT_TRUE(aurore::security::verify_sequence_number(0, max_uint32));
    
    // Expected = max-1, current = 0 (valid wrap)
    ASSERT_TRUE(aurore::security::verify_sequence_number(0, max_uint32 - 1));
    
    // Expected = max-100, current = 50 (valid wrap)
    ASSERT_TRUE(aurore::security::verify_sequence_number(50, max_uint32 - 100));
}

TEST(test_sequence_wrap_around_boundary) {
    // Test boundary conditions for wrap-around
    uint32_t half_range = 1u << 31;  // 2^31
    
    // At the boundary: diff == 2^31 should be INVALID (ambiguous)
    // current = 0, expected = 2^31 -> diff = 2^31 (exactly at boundary, ambiguous)
    // Per RFC 1982, this is ambiguous and should be treated as invalid
    ASSERT_FALSE(aurore::security::verify_sequence_number(0, half_range));
    
    // Just inside valid range: diff = 2^31 - 1 < 2^31
    // current = 0, expected = 2^31 - 1 -> diff = 0 - (2^31-1) = 2^31 + 1 (mod 2^32)
    // This is > 2^31, so INVALID
    ASSERT_FALSE(aurore::security::verify_sequence_number(0, half_range - 1));
    
    // Just inside valid range: current = 1, expected = 2^31
    // diff = 1 - 2^31 = 2^31 + 1 (mod 2^32) > 2^31, INVALID
    ASSERT_FALSE(aurore::security::verify_sequence_number(1, half_range));
    
    // Valid wrap: current = 0, expected = 2^31 + 1
    // diff = 0 - (2^31 + 1) = 2^31 - 1 (mod 2^32) < 2^31, VALID
    ASSERT_TRUE(aurore::security::verify_sequence_number(0, half_range + 1));
}

TEST(test_sequence_large_backward_jump) {
    // Large backward jump that wraps into valid range should be VALID
    // This is correct RFC 1982 behavior - the "window" wraps around
    
    uint32_t large_expected = 3000000000u;  // 0xB2D05E00
    // Valid window: [3000000000, 4294967295] U [0, 852516351]
    // current = 1 is in [0, 852516351], so VALID
    ASSERT_TRUE(aurore::security::verify_sequence_number(1, large_expected));
    
    // current = 1000000000 is NOT in valid window, INVALID
    ASSERT_FALSE(aurore::security::verify_sequence_number(1000000000u, large_expected));
}

TEST(test_sequence_zero_initialization) {
    // First message with sequence 0 should be valid
    ASSERT_TRUE(aurore::security::verify_sequence_number(0, 0));
    
    // Second message with sequence 1 should be valid
    ASSERT_TRUE(aurore::security::verify_sequence_number(1, 0));
}

TEST(test_sequence_consecutive) {
    // Consecutive sequence numbers should all be valid
    for (uint32_t i = 0; i < 1000; i++) {
        ASSERT_TRUE(aurore::security::verify_sequence_number(i + 1, i));
    }
}

// ============================================================================
// Sequence Gap Detection Tests
// ============================================================================

TEST(test_gap_no_gap) {
    // Consecutive numbers: no gap
    ASSERT_FALSE(aurore::security::is_sequence_gap(10, 11, 100));
    ASSERT_FALSE(aurore::security::is_sequence_gap(100, 101, 100));
    ASSERT_FALSE(aurore::security::is_sequence_gap(0, 1, 100));
}

TEST(test_gap_small_gap) {
    // Small gaps below threshold
    ASSERT_FALSE(aurore::security::is_sequence_gap(10, 20, 100));
    ASSERT_FALSE(aurore::security::is_sequence_gap(100, 150, 100));
    ASSERT_FALSE(aurore::security::is_sequence_gap(0, 99, 100));
}

TEST(test_gap_threshold_boundary) {
    // Test boundary conditions
    ASSERT_FALSE(aurore::security::is_sequence_gap(0, 100, 100));   // gap == threshold
    ASSERT_TRUE(aurore::security::is_sequence_gap(0, 101, 100));    // gap > threshold
    ASSERT_FALSE(aurore::security::is_sequence_gap(0, 99, 100));    // gap < threshold
}

TEST(test_gap_large_gap) {
    // Large gaps exceeding threshold
    ASSERT_TRUE(aurore::security::is_sequence_gap(10, 200, 100));
    ASSERT_TRUE(aurore::security::is_sequence_gap(100, 1200, 100));
    ASSERT_TRUE(aurore::security::is_sequence_gap(0, 1001, 100));
}

TEST(test_gap_wrap_around) {
    // Gap with wrap-around
    uint32_t max_uint32 = 0xFFFFFFFFu;
    
    // Small gap with wrap: should NOT trigger
    // old = max-5, new = 5 -> gap = 10 (wrapping from max to 5)
    // Gap calculation: 5 - (max-5) = 5 - 0xFFFFFFFA = 10 (mod 2^32)
    ASSERT_FALSE(aurore::security::is_sequence_gap(max_uint32 - 5, 5, 100));
    
    // Large gap with wrap: should trigger
    // old = max-1000, new = 100 -> gap = 1100
    // Gap calculation: 100 - (max-1000) = 100 - 0xFFFFFC18 = 1104 (mod 2^32)
    ASSERT_TRUE(aurore::security::is_sequence_gap(max_uint32 - 1000, 100, 100));
}

TEST(test_gap_backward_jump) {
    // Backward jump (replay) should be treated as huge gap
    ASSERT_TRUE(aurore::security::is_sequence_gap(100, 10, 1000));
    ASSERT_TRUE(aurore::security::is_sequence_gap(1000, 1, 1000));
}

TEST(test_gap_same_sequence) {
    // Same sequence: no gap
    ASSERT_FALSE(aurore::security::is_sequence_gap(100, 100, 100));
    ASSERT_FALSE(aurore::security::is_sequence_gap(0, 0, 100));
}

TEST(test_gap_security_fault_threshold) {
    // Test security fault threshold (1000)
    ASSERT_FALSE(aurore::security::is_sequence_gap(0, 1000, 1000));      // At threshold
    ASSERT_TRUE(aurore::security::is_sequence_gap(0, 1001, 1000));       // Exceeds threshold
    
    // Real-world scenario: gap > 1000 triggers fault
    ASSERT_TRUE(aurore::security::is_sequence_gap(100, 1200, 1000));
}

TEST(test_gap_reauth_threshold) {
    // Test re-authentication threshold (100)
    ASSERT_FALSE(aurore::security::is_sequence_gap(0, 100, 100));        // At threshold
    ASSERT_TRUE(aurore::security::is_sequence_gap(0, 101, 100));         // Exceeds threshold
    
    // Real-world scenario: gap > 100 requires re-auth
    ASSERT_TRUE(aurore::security::is_sequence_gap(50, 151, 100));
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(test_sequence_gap_combined) {
    // Test verify + gap detection together
    
    // Valid sequence with no gap
    uint32_t seq = 100;
    ASSERT_TRUE(aurore::security::verify_sequence_number(seq, 100));
    ASSERT_FALSE(aurore::security::is_sequence_gap(100, seq, 100));
    
    // Valid sequence with small gap
    seq = 150;
    ASSERT_TRUE(aurore::security::verify_sequence_number(seq, 100));
    ASSERT_FALSE(aurore::security::is_sequence_gap(100, seq, 100));
    
    // Valid sequence with large gap (re-auth required)
    seq = 250;
    ASSERT_TRUE(aurore::security::verify_sequence_number(seq, 100));
    ASSERT_TRUE(aurore::security::is_sequence_gap(100, seq, 100));
    
    // Invalid sequence (replay)
    seq = 50;
    ASSERT_FALSE(aurore::security::verify_sequence_number(seq, 100));
}

TEST(test_sequence_stress_consecutive) {
    // Stress test with many consecutive increments
    uint32_t current = 0;
    for (uint32_t i = 0; i < 10000; i++) {
        ASSERT_TRUE(aurore::security::verify_sequence_number(i, current));
        current = i;
    }
}

TEST(test_sequence_stress_wrap) {
    // Stress test wrap-around scenarios
    uint32_t max_uint32 = 0xFFFFFFFFu;
    
    // Approach max
    ASSERT_TRUE(aurore::security::verify_sequence_number(max_uint32 - 1, max_uint32 - 2));
    ASSERT_TRUE(aurore::security::verify_sequence_number(max_uint32, max_uint32 - 1));
    
    // Wrap to 0
    ASSERT_TRUE(aurore::security::verify_sequence_number(0, max_uint32));
    ASSERT_TRUE(aurore::security::verify_sequence_number(1, 0));
    ASSERT_TRUE(aurore::security::verify_sequence_number(2, 1));
}

TEST(test_sequence_gap_stress) {
    // Stress test gap detection with various patterns
    for (uint32_t i = 0; i < 1000; i += 100) {
        ASSERT_FALSE(aurore::security::is_sequence_gap(i, i + 50, 100));
        ASSERT_TRUE(aurore::security::is_sequence_gap(i, i + 150, 100));
    }
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST(test_edge_max_uint32) {
    uint32_t max_uint32 = 0xFFFFFFFFu;
    
    // Max value operations
    ASSERT_TRUE(aurore::security::verify_sequence_number(max_uint32, max_uint32));
    
    // max_uint32 vs 0: diff = max - 0 = max = 0xFFFFFFFF >= 2^31, INVALID
    // This is correct - max is more than half-range ahead of 0
    ASSERT_FALSE(aurore::security::verify_sequence_number(max_uint32, 0));
    
    // 0 vs max_uint32-1: diff = 0 - (max-1) = 1 (mod 2^32) < 2^31, VALID
    ASSERT_TRUE(aurore::security::verify_sequence_number(0, max_uint32 - 1));
    
    // Gap at max
    ASSERT_FALSE(aurore::security::is_sequence_gap(max_uint32 - 50, max_uint32, 100));
}

TEST(test_edge_zero) {
    // Zero value operations
    ASSERT_TRUE(aurore::security::verify_sequence_number(0, 0));
    ASSERT_TRUE(aurore::security::verify_sequence_number(1, 0));
    ASSERT_FALSE(aurore::security::verify_sequence_number(0, 1));
    
    // Gap from zero
    ASSERT_FALSE(aurore::security::is_sequence_gap(0, 50, 100));
    ASSERT_TRUE(aurore::security::is_sequence_gap(0, 150, 100));
}

TEST(test_edge_half_range) {
    // Test around half range (2^31)
    uint32_t half = 1u << 31;
    
    // Around half range
    ASSERT_TRUE(aurore::security::verify_sequence_number(half, half - 1));
    ASSERT_FALSE(aurore::security::verify_sequence_number(half - 1, half));
    
    // Gap around half range
    ASSERT_FALSE(aurore::security::is_sequence_gap(half - 50, half + 50, 200));
    ASSERT_TRUE(aurore::security::is_sequence_gap(half - 50, half + 150, 100));
}

// ============================================================================
// Security Scenario Tests
// ============================================================================

TEST(test_security_replay_attack) {
    // Simulate replay attack scenario
    uint32_t legitimate_seq = 1000;
    
    // Attender replays old message with seq = 500
    ASSERT_FALSE(aurore::security::verify_sequence_number(500, legitimate_seq));
    
    // Attender replays same message again
    ASSERT_FALSE(aurore::security::verify_sequence_number(500, legitimate_seq));
    
    // Legitimate next message
    ASSERT_TRUE(aurore::security::verify_sequence_number(1001, legitimate_seq));
}

TEST(test_security_packet_loss) {
    // Simulate packet loss scenario
    uint32_t last_received = 100;
    
    // Packets 101-105 lost, receive 106
    ASSERT_TRUE(aurore::security::verify_sequence_number(106, last_received));
    ASSERT_TRUE(aurore::security::is_sequence_gap(last_received, 106, 5));  // Gap > 5
    ASSERT_FALSE(aurore::security::is_sequence_gap(last_received, 106, 10)); // Gap <= 10
}

TEST(test_security_dos_large_gap) {
    // Simulate DoS attack with large sequence gap
    uint32_t last_received = 100;
    
    // Attacker sends message with huge gap
    ASSERT_TRUE(aurore::security::verify_sequence_number(2000000, last_received));
    ASSERT_TRUE(aurore::security::is_sequence_gap(last_received, 2000000, 1000));  // Security fault
    
    // Should trigger security fault handling
}

// ============================================================================
// Main
// ============================================================================

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    std::cout << "Running RFC 1982 Sequence Validation tests..." << std::endl;
    std::cout << "=============================================" << std::endl;

    // RFC 1982 verification tests
    RUN_TEST(test_sequence_normal_order);
    RUN_TEST(test_sequence_exact_match);
    RUN_TEST(test_sequence_replay_detection);
    RUN_TEST(test_sequence_wrap_around_small);
    RUN_TEST(test_sequence_wrap_around_boundary);
    RUN_TEST(test_sequence_large_backward_jump);
    RUN_TEST(test_sequence_zero_initialization);
    RUN_TEST(test_sequence_consecutive);

    // Gap detection tests
    RUN_TEST(test_gap_no_gap);
    RUN_TEST(test_gap_small_gap);
    RUN_TEST(test_gap_threshold_boundary);
    RUN_TEST(test_gap_large_gap);
    RUN_TEST(test_gap_wrap_around);
    RUN_TEST(test_gap_backward_jump);
    RUN_TEST(test_gap_same_sequence);
    RUN_TEST(test_gap_security_fault_threshold);
    RUN_TEST(test_gap_reauth_threshold);

    // Integration tests
    RUN_TEST(test_sequence_gap_combined);
    RUN_TEST(test_sequence_stress_consecutive);
    RUN_TEST(test_sequence_stress_wrap);
    RUN_TEST(test_sequence_gap_stress);

    // Edge case tests
    RUN_TEST(test_edge_max_uint32);
    RUN_TEST(test_edge_zero);
    RUN_TEST(test_edge_half_range);

    // Security scenario tests
    RUN_TEST(test_security_replay_attack);
    RUN_TEST(test_security_packet_loss);
    RUN_TEST(test_security_dos_large_gap);

    // Summary
    std::cout << "\n=============================================" << std::endl;
    std::cout << "Tests run:     " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed:  " << g_tests_passed.load() << std::endl;
    std::cout << "Tests failed:  " << g_tests_failed.load() << std::endl;

    return g_tests_failed.load() > 0 ? 1 : 0;
}
