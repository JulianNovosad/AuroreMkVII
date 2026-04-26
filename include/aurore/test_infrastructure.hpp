#ifndef AURORE_TEST_INFRASTRUCTURE_HPP
#define AURORE_TEST_INFRASTRUCTURE_HPP

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <map>

namespace aurore {
namespace test {

// ============================================================================
// StackTracker
// ============================================================================

class StackTracker {
public:
    struct StackStats {
        uint64_t current_usage_bytes = 0;
        uint64_t high_water_bytes = 0;
        uint64_t max_stack_size_bytes = 0;
        uint64_t safety_margin_bytes = 0;
        bool margin_ok = false;
    };

    StackTracker(uint64_t max_stack_size_kb);
    void record_sample();
    StackStats get_stats() const;
    bool check_safety_threshold(uint64_t safety_margin_kb) const;

private:
    uint64_t max_stack_size_bytes_;
    mutable uint64_t high_water_bytes_;
};

// ============================================================================
// HeapTracker
// ============================================================================

class HeapTracker {
public:
    struct HeapStats {
        uint64_t allocation_count = 0;
        uint64_t deallocation_count = 0;
        uint64_t current_allocated_bytes = 0;
        uint64_t peak_allocated_bytes = 0;
        uint64_t total_allocated_bytes = 0;
        uint64_t total_freed_bytes = 0;
        bool has_leak = false;
        bool growth_trend_exceeds_baseline = false;
        double fragmentation_ratio = 0.0;
    };

    HeapTracker();
    void record_allocation(size_t size);
    void record_deallocation(size_t size);
    HeapStats get_stats() const;
    void set_baseline_envelope(size_t max_growth_bytes);
    bool check_growth_envelope() const;
    void reset();

private:
    std::atomic<uint64_t> allocation_count_;
    std::atomic<uint64_t> deallocation_count_;
    std::atomic<size_t> current_allocated_bytes_;
    std::atomic<size_t> peak_allocated_bytes_;
    std::atomic<size_t> total_allocated_bytes_;
    std::atomic<size_t> total_freed_bytes_;
    std::vector<size_t> size_history_;
    size_t baseline_envelope_bytes_;
};

// ============================================================================
// DmaHealthMonitor
// ============================================================================

enum class DmaState {
    Idle,
    Active,
    Error,
    Recovering
};

class DmaHealthMonitor {
public:
    struct DmaStats {
        DmaState state = DmaState::Idle;
        uint64_t transfer_count = 0;
        uint64_t error_count = 0;
        uint64_t last_transfer_ns = 0;
        size_t buffer_alignment = 0;
        bool alignment_valid = false;
    };

    DmaHealthMonitor();
    void record_transfer(size_t bytes, uint64_t duration_ns);
    void record_error();
    void simulate_fault();
    bool recover();
    DmaStats get_stats() const;
    bool check_alignment(size_t alignment) const;

private:
    DmaState state_;
    uint64_t transfer_count_;
    uint64_t error_count_;
    uint64_t last_transfer_ns_;
    size_t buffer_alignment_;
    uint64_t total_bytes_transferred_;
};

// ============================================================================
// ThermalHealthMonitor
// ============================================================================

enum class ThrottleState {
    Nominal,
    Throttling,
    Critical
};

class ThermalHealthMonitor {
public:
    struct ThermalStats {
        double temperature_celsius = 0.0;
        ThrottleState throttle_state = ThrottleState::Nominal;
        uint64_t throttle_count = 0;
        uint64_t throttle_duration_ns = 0;
        bool frequency_scaled = false;
    };

    ThermalHealthMonitor(double critical_threshold_celsius = 80.0);
    void update_temperature(double celsius);
    ThrottleState check_throttle_state() const;
    void record_throttle_event();
    ThermalStats get_stats() const;
    bool simulate_throttling_transition();
    bool verify_timing_contract() const;

private:
    double critical_threshold_celsius_;
    ThrottleState current_state_;
    uint64_t throttle_count_;
    uint64_t total_throttle_duration_ns_;
    std::deque<double> temperature_history_;
};

// ============================================================================
// ResourceMonitor
// ============================================================================

enum class ResourceType {
    Handles,
    Descriptors,
    DmaChannels,
    MemoryBlocks
};

class ResourceMonitor {
public:
    struct ResourceStats {
        ResourceType type = ResourceType::Handles;
        uint32_t limit = 0;
        uint32_t active_count = 0;
        uint32_t peak_count = 0;
        bool exhausted = false;
    };

    ResourceMonitor(ResourceType type, uint32_t limit);
    bool acquire();
    void release();
    ResourceStats get_stats() const;
    bool is_exhausted() const;

private:
    ResourceType type_;
    uint32_t limit_;
    std::atomic<uint32_t> active_count_;
    uint32_t peak_count_;
};

// ============================================================================
// QueueStressTest
// ============================================================================

enum class BackpressurePolicy {
    DropOldest,
    DropNewest,
    BlockProducer,
    ReturnFalse
};

class QueueStressTest {
public:
    struct QueueMetrics {
        uint64_t total_produced = 0;
        uint64_t total_consumed = 0;
        uint64_t total_dropped = 0;
        size_t current_depth = 0;
        size_t max_depth = 0;
        bool backpressure_triggered = false;
    };

    QueueStressTest(size_t capacity, BackpressurePolicy policy);
    bool push(const void* data, size_t size);
    bool pop(void* data, size_t& size);
    QueueMetrics get_metrics() const;
    void reset_metrics();

private:
    size_t capacity_;
    BackpressurePolicy policy_;
    std::atomic<size_t> depth_;
    std::atomic<uint64_t> total_produced_;
    std::atomic<uint64_t> total_consumed_;
    std::atomic<uint64_t> total_dropped_;
    std::atomic<size_t> max_depth_;
};

// ============================================================================
// ConcurrencyPathologyDetector
// ============================================================================

enum class PathologyType {
    None,
    Deadlock,
    Livelock,
    Starvation,
    PriorityInversion,
    DataCoherency
};

class ConcurrencyPathologyDetector {
public:
    struct PathologyReport {
        PathologyType type = PathologyType::None;
        std::vector<void*> involved_locks;
        uint64_t detected_latency_ns = 0;
    };

    ConcurrencyPathologyDetector();
    void enable_instrumentation(bool enable);
    void register_lock(void* lock_id);
    void register_acquire(void* lock_id, int priority = 0);
    void release(void* lock_id);
    PathologyReport check();

private:
    std::atomic<bool> instrumentation_enabled_;
    std::vector<void*> lock_chain_;
    uint64_t lockwait_time_ns_;
};

// ============================================================================
// TimestampValidator
// ============================================================================

enum class ViolationType {
    None,
    NonMonotonic,
    Discontinuity,
    Rollover
};

class TimestampValidator {
public:
    struct ValidationReport {
        ViolationType type = ViolationType::None;
        uint64_t total_samples = 0;
        uint64_t total_violations = 0;
        uint64_t first_violation_timestamp_ns = 0;
        uint64_t second_violation_timestamp_ns = 0;
        int64_t diff_ns = 0;
    };

    TimestampValidator(uint64_t max_acceptable_jitter_ns = 100000); // 100 microseconds
    void record_timestamp(uint64_t timestamp_ns);
    ValidationReport validate() const;
    void reset();

private:
    uint64_t max_acceptable_jitter_ns_;
    std::deque<uint64_t> timestamps_;
    std::atomic<uint64_t> violation_count_;
};

// ============================================================================
// NumericRobustnessTester
// ============================================================================

class NumericRobustnessTester {
public:
    struct NumericTestResult {
        bool nan_propagated = false;
        bool inf_propagated = false;
        bool overflow_detected = false;
        bool underflow_detected = false;
        float min_value = 0.0f;
        float max_value = 0.0f;
        float max_precision_loss_ulps = 0.0f;
        bool precision_decay_excessive = false;
    };

    static bool is_nan(float v);
    static bool is_inf(float v);
    static bool is_finite(float v);
    static int32_t float_to_int32_saturating(float v);
    static uint32_t float_to_uint32_saturating(float v);
    static NumericTestResult test_operations(float a, float b, uint32_t iterations);
    static NumericTestResult test_trig(float v, uint32_t iterations);
    static NumericTestResult test_angle_wrapping(float v, uint32_t iterations);
};

// ============================================================================
// HostileInputInjector
// ============================================================================

class HostileInputInjector {
public:
    struct Packet {
        void* data = nullptr;
        size_t size = 0;
        uint64_t timestamp_ns = 0;
        uint32_t sequence = 0;
    };

    static Packet truncate(Packet original, size_t new_size);
    static Packet duplicate(Packet original);
    static Packet delay(Packet original, uint64_t delay_ns);
    static Packet replay(Packet original, uint32_t original_seq);
    static Packet inject_extreme_value(Packet original, size_t offset);
    static bool validate_packets(Packet* packets, size_t count);
};

// ============================================================================
// ResetScenarioTester
// ============================================================================

enum class ResetType {
    Warm,
    Cold,
    BrownOut,
    SubsystemPartial
};

class ResetScenarioTester {
public:
    struct RecoveryMetrics {
        ResetType type = ResetType::Warm;
        uint64_t recovery_time_ns = 0;
        bool deterministic = false;
    };

    static RecoveryMetrics test_reset(ResetType type, void* state_context);
    static bool verify_no_stale_state(void* state_context, size_t state_size);
    static bool verify_deterministic_recovery(ResetType type, uint32_t iterations);
};

// ============================================================================
// ConfigReloadTester
// ============================================================================

class ConfigReloadTester {
public:
    struct ConfigInfo {
        uint32_t version = 0;
        bool valid = false;
        std::map<std::string, std::string> settings;
    };

    ConfigReloadTester();
    bool load_config(const char* config_name);
    bool reload_config(const char* config_name);
    bool rollback_to_last_known_good();
    ConfigInfo get_active_config() const;

private:
    ConfigInfo active_config_;
    ConfigInfo last_known_good_;
};

// ============================================================================
// LogTester
// ============================================================================

class LogTester {
public:
    struct CompletenessReport {
        uint64_t events_logged = 0;
        double completeness_ratio = 0.0;
    };

    struct OrderReport {
        uint64_t total_events = 0;
        uint64_t out_of_order_count = 0;
        bool chronological = true;
    };

    LogTester();
    void log_event(int event_id, const char* event_type);
    CompletenessReport get_completeness_report() const;
    OrderReport get_order_report() const;

private:
    mutable std::mutex mutex_;
    std::deque<uint64_t> timestamps_;
    std::deque<int> event_ids_;
};

// ============================================================================
// OffsetTracker
// ============================================================================

class OffsetTracker {
public:
    OffsetTracker(uint64_t reference_ns = 0);
    void record_sample();
    uint64_t get_offset_ns() const;

private:
    uint64_t reference_ns_;
    std::deque<uint64_t> offsets_;
};

// ============================================================================
// FaultTimeline
// ============================================================================

class FaultTimeline {
public:
    struct FaultEntry {
        uint8_t fault_code = 0;
        uint64_t timestamp_ns = 0;
    };

    FaultTimeline();
    void record_fault(uint8_t fault, uint64_t timestamp_ns);
    std::vector<FaultEntry> get_fault_sequence() const;

private:
    std::vector<FaultEntry> faults_;
};

// ============================================================================
// CrashDumpTester
// ============================================================================

class CrashDumpTester {
public:
    CrashDumpTester();
    void record_state(uint32_t thread_id, const char* state_name, const char* state_value);
    void set_state(const void* data, size_t size);
    void finalize();
    bool verify_integrity() const;
    bool verify_order() const;
    size_t get_state_size() const;

private:
    std::vector<uint64_t> timestamps_;
    std::vector<uint8_t> dump_data_;
};

// ============================================================================
// FaultInjector
// ============================================================================

enum class FaultTarget {
    None,
    Camera,
    TPU,
    Gimbal,
    Network,
    Memory,
    Power,
    SoftwareLogic
};

enum class FaultType {
    Transient,
    Permanent,
    Intermittent,
    CorruptData,
    HighLatency
};

class FaultInjector {
public:
    static void inject_fault(FaultTarget target, FaultType type, uint64_t duration_ns);
    static void clear_faults();
    static bool validate_isolation(FaultTarget triggered, FaultTarget affected);
    static bool validate_degradation(FaultTarget failed, bool graceful);
    static bool validate_fail_safe(FaultTarget failed);
};

// ============================================================================
// TestResultAggregator
// ============================================================================

enum class TestTier {
    Tier0_FastUnit = 0,
    Tier1_SafetyState = 1,
    Tier2_RealtimeTemporal = 2,
    Tier3_StressSoak = 3,
    Tier4_HIL = 4,
    Count // Keep this last for enum size
};

class TestResultAggregator {
public:
    struct TierResult {
        TestTier tier;
        uint64_t tests_run;
        uint64_t tests_passed;
        uint64_t tests_failed;
        uint64_t tests_skipped;
        uint64_t total_time_ns;
        bool blocks_merge;
    };

    static void record_result(TestTier tier, bool passed, uint64_t duration_ns);
    static TierResult get_tier_result(TestTier tier);
    static bool should_block_merge();
    static void print_summary();
    static void reset();

private:
    static std::vector<TierResult> tier_results_;
};

// ============================================================================
// TestEnvironment (Global Test Setup/Teardown)
// ============================================================================

class TestEnvironment {
public:
    static void init(uint64_t max_stack_size_kb, size_t heap_baseline_envelope_bytes);
    static void reset_trackers();
    static StackTracker& get_stack_tracker();
    static HeapTracker& get_heap_tracker();

private:
    static StackTracker* stack_tracker_instance_;
    static HeapTracker* heap_tracker_instance_;
};

} // namespace test
} // namespace aurore

#endif // AURORE_TEST_INFRASTRUCTURE_HPP
