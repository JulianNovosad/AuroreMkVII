#include <cmath>
#include <iostream>
#include <atomic>
#include <chrono>

#include "trigger_sanity_check.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_TRUE(condition) do { \
    if (!(condition)) { \
        std::cerr << "FAILED: " << #condition << "\n"; \
        throw std::runtime_error("assertion failed"); \
    } \
} while(0)

#define ASSERT_FALSE(condition) do { \
    if (condition) { \
        std::cerr << "FAILED: NOT " << #condition << "\n"; \
        throw std::runtime_error("assertion failed"); \
    } \
} while(0)

#define ASSERT_NEAR(actual, expected, tolerance) do { \
    float _actual = (actual); \
    float _expected = (expected); \
    float _tolerance = (tolerance); \
    float _diff = std::abs(_actual - _expected); \
    if (_diff > _tolerance) { \
        std::cerr << "FAILED: " << #actual << " (" << _actual << ") not near " << \
                  #expected << " within " << _tolerance << "\n"; \
        throw std::runtime_error("assertion failed"); \
    } \
} while(0)

#define RUN_TEST(name) do { \
    std::cout << "Running " << name << "... "; \
    try { \
        name(); \
        std::cout << "PASSED\n"; \
        tests_passed++; \
    } catch (const std::exception& e) { \
        std::cout << "FAILED: " << e.what() << "\n"; \
        tests_failed++; \
    } \
} while(0)

static void trigger_position_within_tolerance();
static void trigger_position_exceeds_tolerance();
static void trigger_position_both_zero();
static void trigger_timing_within_timeout();
static void trigger_timing_timeout();
static void consecutive_failure_detection();
static void duty_cycle_validation();
static void period_validation();
static void edge_detection();
static void reset_counters();

void trigger_position_within_tolerance() {
    aurore::servo::TriggerSanityChecker checker;
    checker.set_tolerance(5.0f);

    auto result = checker.verify_trigger_position(0.5f, 0.52f);

    ASSERT_TRUE(result.is_valid);
    ASSERT_TRUE(result.deviation_pct < 5.0f);
}

void trigger_position_exceeds_tolerance() {
    aurore::servo::TriggerSanityChecker checker;
    checker.set_tolerance(5.0f);

    auto result = checker.verify_trigger_position(0.5f, 0.6f);

    ASSERT_FALSE(result.is_valid);
    ASSERT_TRUE(result.deviation_pct > 5.0f);
    ASSERT_TRUE(result.error_message.find("DEVIATION") != std::string::npos);
}

void trigger_position_both_zero() {
    aurore::servo::TriggerSanityChecker checker;

    auto result = checker.verify_trigger_position(0.0f, 0.0f);

    ASSERT_TRUE(result.is_valid);
    ASSERT_TRUE(result.error_message.find("zero") != std::string::npos);
}

void trigger_timing_within_timeout() {
    aurore::servo::TriggerSanityChecker checker;
    checker.set_timeout(100);

    auto result = checker.verify_trigger_timing(1000, 1050);

    ASSERT_TRUE(result.is_valid);
    ASSERT_TRUE(result.error_message.find("TIMING OK") != std::string::npos);
}

void trigger_timing_timeout() {
    aurore::servo::TriggerSanityChecker checker;
    checker.set_timeout(100);

    auto result = checker.verify_trigger_timing(1000, 1200);

    ASSERT_FALSE(result.is_valid);
    ASSERT_TRUE(result.error_message.find("TIMEOUT") != std::string::npos);
}

void consecutive_failure_detection() {
    aurore::servo::TriggerSanityChecker checker;

    ASSERT_TRUE(checker.is_healthy());

    checker.verify_trigger_position(0.5f, 0.9f);
    ASSERT_TRUE(checker.is_healthy());

    checker.verify_trigger_position(0.5f, 0.9f);
    ASSERT_TRUE(checker.is_healthy());

    checker.verify_trigger_position(0.5f, 0.9f);
    ASSERT_FALSE(checker.is_healthy());
}

void duty_cycle_validation() {
    aurore::servo::TriggerCorrelationValidator validator;
    ASSERT_TRUE(validator.initialize(50.0f, 5.0f));

    auto result1 = validator.validate_duty_cycle(52.0f);
    ASSERT_TRUE(result1.is_valid);

    auto result2 = validator.validate_duty_cycle(40.0f);
    ASSERT_FALSE(result2.is_valid);
}

void period_validation() {
    aurore::servo::TriggerCorrelationValidator validator;
    ASSERT_TRUE(validator.initialize(50.0f, 5.0f));

    auto result1 = validator.validate_period(3000.0f, 3003.0f);
    ASSERT_TRUE(result1.is_valid);

    auto result2 = validator.validate_period(3500.0f, 3003.0f);
    ASSERT_FALSE(result2.is_valid);
}

void edge_detection() {
    aurore::servo::TriggerCorrelationValidator validator;
    ASSERT_TRUE(validator.initialize(50.0f, 5.0f));

    ASSERT_TRUE(validator.get_edge_count() == 0);

    validator.validate_rising_edge(1000000);
    ASSERT_TRUE(validator.get_edge_count() == 1);

    validator.validate_rising_edge(2000000);
    ASSERT_TRUE(validator.get_edge_count() == 2);
}

void reset_counters() {
    aurore::servo::TriggerSanityChecker checker;

    checker.verify_trigger_position(0.5f, 0.9f);
    checker.verify_trigger_position(0.5f, 0.9f);
    checker.verify_trigger_position(0.5f, 0.9f);

    ASSERT_TRUE(checker.get_total_checks() == 3);
    ASSERT_TRUE(checker.get_failed_checks() == 3);

    checker.reset_counters();

    ASSERT_TRUE(checker.get_total_checks() == 0);
    ASSERT_TRUE(checker.get_failed_checks() == 0);
    ASSERT_TRUE(checker.is_healthy());
}

int main() {
    std::cout << "=== Trigger Sanity Check Tests ===\n\n";

    std::cout << "[Position Validation Tests]\n";
    RUN_TEST(trigger_position_within_tolerance);
    RUN_TEST(trigger_position_exceeds_tolerance);
    RUN_TEST(trigger_position_both_zero);

    std::cout << "\n[Timing Validation Tests]\n";
    RUN_TEST(trigger_timing_within_timeout);
    RUN_TEST(trigger_timing_timeout);

    std::cout << "\n[Health Monitoring Tests]\n";
    RUN_TEST(consecutive_failure_detection);
    RUN_TEST(reset_counters);

    std::cout << "\n[Correlation Validation Tests]\n";
    RUN_TEST(duty_cycle_validation);
    RUN_TEST(period_validation);
    RUN_TEST(edge_detection);

    std::cout << "\n=== Test Results ===\n";
    std::cout << "Passed: " << tests_passed << "\n";
    std::cout << "Failed: " << tests_failed << "\n";

    return tests_failed > 0 ? 1 : 0;
}
