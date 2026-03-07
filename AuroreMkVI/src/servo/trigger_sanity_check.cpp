#include "trigger_sanity_check.h"
#include <cmath>
#include <sstream>

namespace aurore {
namespace servo {

TriggerSanityChecker::TriggerSanityChecker()
    : tolerance_pct_(TRIGGER_DEVIATION_TOLERANCE_PCT),
      timeout_ms_(TRIGGER_TIMEOUT_MS),
      total_checks_(0),
      failed_checks_(0),
      consecutive_failures_(0),
      is_unhealthy_(false) {
}

TriggerSanityChecker::~TriggerSanityChecker() {}

TriggerCorrelationValidator::TriggerCorrelationValidator()
    : expected_duty_cycle_pct_(0),
      tolerance_pct_(5.0f),
      edge_count_(0),
      glitch_count_(0) {
}

TriggerCorrelationValidator::~TriggerCorrelationValidator() {}

bool TriggerCorrelationValidator::initialize(float expected_duty_cycle_pct, float tolerance_pct) {
    if (expected_duty_cycle_pct < 0.0f || expected_duty_cycle_pct > 100.0f) {
        return false;
    }
    expected_duty_cycle_pct_ = expected_duty_cycle_pct;
    tolerance_pct_ = tolerance_pct;
    return true;
}

void TriggerSanityChecker::set_tolerance(float tolerance_pct) {
    tolerance_pct_.store(tolerance_pct, std::memory_order_release);
}

void TriggerSanityChecker::set_timeout(uint64_t timeout_ms) {
    timeout_ms_.store(timeout_ms, std::memory_order_release);
}

TriggerCheckResult TriggerSanityChecker::verify_trigger_position(float predicted_position,
                                                                  float actual_position,
                                                                  uint64_t timestamp_ms) {
    TriggerCheckResult result;
    result.timestamp_ms = timestamp_ms > 0 ? timestamp_ms :
                          static_cast<uint64_t>(
                              std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now().time_since_epoch()).count());

    result.predicted_position = predicted_position;
    result.actual_position = actual_position;

    if (predicted_position == 0.0f && actual_position == 0.0f) {
        result.is_valid = true;
        result.error_message = "TRIGGER_CHECK: Both zero (no trigger)";
        total_checks_.fetch_add(1, std::memory_order_relaxed);
        return result;
    }

    if (predicted_position < 0.0f || predicted_position > 1.0f) {
        result.is_valid = false;
        result.error_message = "TRIGGER_CHECK: Invalid predicted position " +
                               std::to_string(predicted_position);
        failed_checks_.fetch_add(1, std::memory_order_relaxed);
        consecutive_failures_.fetch_add(1, std::memory_order_relaxed);
        if (consecutive_failures_.load(std::memory_order_acquire) >= MAX_CONSECUTIVE_FAILURES) {
            is_unhealthy_.store(true, std::memory_order_release);
        }
        total_checks_.fetch_add(1, std::memory_order_relaxed);
        return result;
    }

    float tolerance = tolerance_pct_.load(std::memory_order_acquire);
    float deviation = 0.0f;

    if (std::abs(predicted_position) > 0.001f) {
        deviation = std::abs((actual_position - predicted_position) / predicted_position) * 100.0f;
    } else if (std::abs(actual_position) > 0.001f) {
        deviation = 100.0f;
    }

    result.deviation_pct = deviation;

    if (deviation <= tolerance) {
        result.is_valid = true;
        result.error_message = "TRIGGER_CHECK: PASSED (deviation " +
                               std::to_string(deviation) + "% <= " +
                               std::to_string(tolerance) + "%)";
        consecutive_failures_.store(0, std::memory_order_release);
    } else {
        result.is_valid = false;
        std::ostringstream oss;
        oss << "TRIGGER_CHECK: DEVIATION " << deviation << "% exceeds tolerance "
            << tolerance << "%";
        result.error_message = oss.str();
        failed_checks_.fetch_add(1, std::memory_order_relaxed);
        consecutive_failures_.fetch_add(1, std::memory_order_relaxed);
        if (consecutive_failures_.load(std::memory_order_acquire) >= MAX_CONSECUTIVE_FAILURES) {
            is_unhealthy_.store(true, std::memory_order_release);
        }
    }

    total_checks_.fetch_add(1, std::memory_order_relaxed);
    return result;
}

TriggerCheckResult TriggerSanityChecker::verify_trigger_timing(uint64_t expected_timestamp_ms,
                                                               uint64_t actual_timestamp_ms) {
    TriggerCheckResult result;
    result.timestamp_ms = expected_timestamp_ms;
    result.predicted_position = static_cast<float>(expected_timestamp_ms);
    result.actual_position = static_cast<float>(actual_timestamp_ms);

    uint64_t timeout = timeout_ms_.load(std::memory_order_acquire);

    if (actual_timestamp_ms > expected_timestamp_ms + timeout) {
        result.is_valid = false;
        result.deviation_pct = static_cast<float>(
            (actual_timestamp_ms - expected_timestamp_ms) * 100.0 / expected_timestamp_ms);
        result.error_message = "TRIGGER_CHECK: TIMEOUT (" +
                               std::to_string(actual_timestamp_ms - expected_timestamp_ms) +
                               "ms late, limit " + std::to_string(timeout) + "ms)";
        failed_checks_.fetch_add(1, std::memory_order_relaxed);
        consecutive_failures_.fetch_add(1, std::memory_order_relaxed);
    } else {
        result.is_valid = true;
        result.deviation_pct = 0.0f;
        result.error_message = "TRIGGER_CHECK: TIMING OK";
        consecutive_failures_.store(0, std::memory_order_release);
    }

    total_checks_.fetch_add(1, std::memory_order_relaxed);
    return result;
}

uint64_t TriggerSanityChecker::get_total_checks() const {
    return total_checks_.load(std::memory_order_acquire);
}

uint64_t TriggerSanityChecker::get_failed_checks() const {
    return failed_checks_.load(std::memory_order_acquire);
}

uint64_t TriggerSanityChecker::get_consecutive_failures() const {
    return consecutive_failures_.load(std::memory_order_acquire);
}

void TriggerSanityChecker::reset_counters() {
    total_checks_.store(0, std::memory_order_release);
    failed_checks_.store(0, std::memory_order_release);
    consecutive_failures_.store(0, std::memory_order_release);
    is_unhealthy_.store(false, std::memory_order_release);
}

bool TriggerSanityChecker::is_healthy() const {
    return !is_unhealthy_.load(std::memory_order_acquire);
}

TriggerCheckResult TriggerCorrelationValidator::validate_duty_cycle(float measured_duty_cycle_pct) {
    TriggerCheckResult result;
    result.timestamp_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    float deviation = std::abs(measured_duty_cycle_pct - expected_duty_cycle_pct_);
    result.deviation_pct = deviation;

    if (deviation <= tolerance_pct_) {
        result.is_valid = true;
        result.error_message = "TRIGGER_DUTY_CYCLE: OK (" +
                               std::to_string(measured_duty_cycle_pct) + "% vs expected " +
                               std::to_string(expected_duty_cycle_pct_) + "%)";
    } else {
        result.is_valid = false;
        result.error_message = "TRIGGER_DUTY_CYCLE: MISMATCH (" +
                               std::to_string(measured_duty_cycle_pct) + "% vs expected " +
                               std::to_string(expected_duty_cycle_pct_) + "%, deviation " +
                               std::to_string(deviation) + "%)";
        glitch_count_.fetch_add(1, std::memory_order_relaxed);
    }

    return result;
}

TriggerCheckResult TriggerCorrelationValidator::validate_period(float measured_period_us,
                                                                float expected_period_us) {
    TriggerCheckResult result;
    result.timestamp_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    float deviation_pct = std::abs(measured_period_us - expected_period_us) /
                          expected_period_us * 100.0f;
    result.deviation_pct = deviation_pct;

    if (deviation_pct <= 10.0f) {
        result.is_valid = true;
        result.error_message = "TRIGGER_PERIOD: OK (" +
                               std::to_string(measured_period_us) + "us vs expected " +
                               std::to_string(expected_period_us) + "us)";
    } else {
        result.is_valid = false;
        result.error_message = "TRIGGER_PERIOD: MISMATCH (" +
                               std::to_string(measured_period_us) + "us vs expected " +
                               std::to_string(expected_period_us) + "us)";
        glitch_count_.fetch_add(1, std::memory_order_relaxed);
    }

    return result;
}

TriggerCheckResult TriggerCorrelationValidator::validate_rising_edge(uint64_t timestamp_us) {
    TriggerCheckResult result;
    result.timestamp_ms = static_cast<uint64_t>(timestamp_us / 1000);
    result.is_valid = true;
    result.error_message = "TRIGGER_EDGE: DETECTED";
    edge_count_.fetch_add(1, std::memory_order_relaxed);
    return result;
}

uint64_t TriggerCorrelationValidator::get_edge_count() const {
    return edge_count_.load(std::memory_order_acquire);
}

uint64_t TriggerCorrelationValidator::get_glitch_count() const {
    return glitch_count_.load(std::memory_order_acquire);
}

void TriggerCorrelationValidator::reset() {
    edge_count_.store(0, std::memory_order_release);
    glitch_count_.store(0, std::memory_order_release);
}

}
}
