#ifndef TRIGGER_SANITY_CHECK_H
#define TRIGGER_SANITY_CHECK_H

#include <atomic>
#include <cstdint>
#include <chrono>
#include <string>

namespace aurore {
namespace servo {

constexpr float TRIGGER_DEVIATION_TOLERANCE_PCT = 5.0f;
constexpr uint64_t TRIGGER_TIMEOUT_MS = 100;
constexpr int MAX_CONSECUTIVE_FAILURES = 3;

struct TriggerCheckResult {
    bool is_valid;
    float predicted_position;
    float actual_position;
    float deviation_pct;
    uint64_t timestamp_ms;
    std::string error_message;

    TriggerCheckResult()
        : is_valid(false), predicted_position(0), actual_position(0),
          deviation_pct(0), timestamp_ms(0) {}
};

class TriggerSanityChecker {
public:
    TriggerSanityChecker();
    ~TriggerSanityChecker();

    void set_tolerance(float tolerance_pct);
    void set_timeout(uint64_t timeout_ms);

    TriggerCheckResult verify_trigger_position(float predicted_position,
                                               float actual_position,
                                               uint64_t timestamp_ms = 0);

    TriggerCheckResult verify_trigger_timing(uint64_t expected_timestamp_ms,
                                              uint64_t actual_timestamp_ms);

    uint64_t get_total_checks() const;
    uint64_t get_failed_checks() const;
    uint64_t get_consecutive_failures() const;
    void reset_counters();

    bool is_healthy() const;

private:
    std::atomic<float> tolerance_pct_;
    std::atomic<uint64_t> timeout_ms_;
    std::atomic<uint64_t> total_checks_;
    std::atomic<uint64_t> failed_checks_;
    std::atomic<int> consecutive_failures_;
    std::atomic<bool> is_unhealthy_;
};

class TriggerCorrelationValidator {
public:
    TriggerCorrelationValidator();
    ~TriggerCorrelationValidator();

    bool initialize(float expected_duty_cycle_pct, float tolerance_pct);

    TriggerCheckResult validate_duty_cycle(float measured_duty_cycle_pct);

    TriggerCheckResult validate_period(float measured_period_us,
                                        float expected_period_us);

    TriggerCheckResult validate_rising_edge(uint64_t timestamp_us);

    uint64_t get_edge_count() const;
    uint64_t get_glitch_count() const;
    void reset();

private:
    float expected_duty_cycle_pct_;
    float tolerance_pct_;
    std::atomic<uint64_t> edge_count_;
    std::atomic<uint64_t> glitch_count_;
};

}
}

#endif // TRIGGER_SANITY_CHECK_H
