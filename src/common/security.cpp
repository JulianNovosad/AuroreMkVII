/**
 * @file security.cpp
 * @brief Security utilities for Aurore MkVII
 *
 * Implements RFC 1982 sequence number arithmetic for wrap-aware comparison
 * and sequence gap detection for replay attack prevention.
 */

#include "aurore/security.hpp"

namespace aurore {
namespace security {

/**
 * @brief RFC 1982 sequence number comparison (wrap-aware).
 *
 * Implements RFC 1982 "Serial Number Arithmetic" for 32-bit sequence numbers.
 * The comparison handles wrap-around correctly using unsigned arithmetic.
 *
 * RFC 1982 defines sequence number comparison where:
 * - S1 < S2 if (S1 < S2 and S2 - S1 < 2^31) OR (S1 > S2 and S1 - S2 > 2^31)
 * - S1 > S2 if (S1 > S2 and S1 - S2 < 2^31) OR (S1 < S2 and S2 - S1 > 2^31)
 *
 * For sequence validation, we check if 'current' is >= 'expected' (not a replay).
 * This is true if current is in the range [expected, expected + 2^31 - 1] (mod 2^32).
 *
 * @param current The received sequence number
 * @param expected The expected next sequence number
 * @return true if current >= expected (valid, not a replay)
 */
bool verify_sequence_number(uint32_t current, uint32_t expected) {
    // Use unsigned arithmetic for correct wrap handling
    // current >= expected (not a replay) if:
    // - current - expected < 2^31 (current is ahead or equal, accounting for wrap)

    uint32_t diff = current - expected;  // Unsigned subtraction (wraps correctly)

    // Valid if diff < 2^31 (current is within forward half-range from expected)
    // This covers:
    // - current == expected: diff = 0, valid
    // - current > expected (no wrap): diff > 0 and < 2^31, valid
    // - current wrapped (current small, expected large): diff wraps to large value
    //   but still < 2^31 if it's a valid forward wrap
    return diff < (1u << 31);
}

/**
 * @brief Detect sequence gaps with configurable threshold.
 *
 * Checks if the gap between old and new sequence numbers exceeds threshold.
 * Handles wrap-around correctly using unsigned arithmetic.
 *
 * A gap indicates potential packet loss, network issues, or replay attack.
 * Per security requirements:
 * - Gap > 100: requires re-authentication
 * - Gap > 1000: triggers security fault
 *
 * @param old_seq The previous sequence number
 * @param new_seq The new sequence number
 * @param threshold Maximum allowed gap before triggering alert
 * @return true if gap > threshold (security concern)
 */
bool is_sequence_gap(uint32_t old_seq, uint32_t new_seq, uint32_t threshold) {
    // Calculate gap size using unsigned arithmetic (handles wrap correctly)
    // gap = new_seq - old_seq (mod 2^32)

    uint32_t gap = new_seq - old_seq;  // Unsigned subtraction wraps correctly

    // If gap >= 2^31, this is a backward jump (replay attack or reset)
    // Treat as huge gap (always exceeds threshold)
    if (gap >= (1u << 31)) {
        return true;
    }

    // Check if forward gap exceeds threshold
    return gap > threshold;
}

}  // namespace security
}  // namespace aurore
