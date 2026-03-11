// include/aurore/state_machine.hpp
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

namespace aurore {

/**
 * @brief FcsState - Formal state machine per AM7-L1-MODE-002, AM7-L3-MODE-001
 *
 * State transition table (AM7-L3-MODE-001):
 *
 * | Source     | BOOT | IDLE_SAFE | FREECAM | SEARCH | TRACKING | ARMED | FAULT |
 * |------------|------|-----------|---------|--------|----------|-------|-------|
 * | BOOT       | —    | ✓         | —       | —      | —        | —     | ✓     |
 * | IDLE_SAFE  | —    | —         | ✓       | ✓      | —        | —     | ✓     |
 * | FREECAM    | —    | ✓         | —       | ✓      | —        | —     | ✓     |
 * | SEARCH     | —    | ✓         | —       | —      | ✓        | —     | ✓     |
 * | TRACKING   | —    | ✓         | —       | ✓      | —        | ✓     | ✓     |
 * | ARMED      | —    | ✓         | —       | —      | ✓        | —     | ✓     |
 * | FAULT      | ✓    | ✓         | —       | —      | —        | —     | —     |
 *
 * State diagram (AM7-L3-MODE-002):
 *
 *                         ┌─────────────┐
 *                         │    BOOT     │
 *                         └──────┬──────┘
 *                                │ (init OK)
 *                                ▼
 *                         ┌─────────────┐
 *                    ┌────│  IDLE_SAFE  │◄───┐
 *                    │    └──────┬──────┘    │
 *               (request)       │(request)    │(cancel/fault)
 *                    │          │             │
 *                    ▼          ▼             │
 *               ┌─────────┐ ┌────────┐        │
 *               │ FREECAM │ │ SEARCH │────────┤
 *               └────┬────┘ └───┬────┘        │
 *                    │          │(lock valid)  │
 *                    │          ▼             │
 *                    │    ┌──────────┐        │
 *                    │    │ TRACKING │◄───────┤
 *                    │    └────┬─────┘        │
 *                    │         │(lock+stable) │
 *                    │         ▼              │
 *                    │    ┌────────┐          │
 *                    └────│ ARMED  │──────────┘
 *                         └───┬────┘
 *                             │
 *               (any fault)   │
 *           ┌─────────────────┘
 *           ▼
 *    ┌─────────────┐
 *    │    FAULT    │
 *    └──────┬──────┘
 *           │
 *    (power cycle / manual reset)
 *           │
 *           └──────────────────────┘
 */
enum class FcsState : uint8_t {
    BOOT = 0,       // AM7-L2-MODE-001: Hardware init, memory lock, self-test
    IDLE_SAFE = 1,  // AM7-L2-MODE-002: Inhibit, gimbal hold, no lock
    FREECAM = 2,    // AM7-L2-MODE-003: Manual gimbal control, no auto-lock
    SEARCH = 3,     // AM7-L2-MODE-004: Auto FOV scan, target acquisition
    TRACKING = 4,   // AM7-L2-MODE-005: Continuous target lock, gimbal active
    ARMED = 5,      // AM7-L2-MODE-006: Interlock enable permitted
    FAULT = 6,      // AM7-L2-MODE-007: Forced inhibit, gimbal hold
};

struct Detection {
    int id{-1};                          ///< Target/marker ID (-1 for visual detection)
    float confidence{0.0f};
    struct {
        int x, y, w, h;
    } bbox{};
    float cx() const { return static_cast<float>(bbox.x) + static_cast<float>(bbox.w) * 0.5f; }
    float cy() const { return static_cast<float>(bbox.y) + static_cast<float>(bbox.h) * 0.5f; }
};

// AM7-L2-TGT-003: Position history entry for 3-frame stability validation
struct PositionHistoryEntry {
    float x{0.f};
    float y{0.f};
    uint64_t timestamp_ns{0};
};

// Note: Named GimbalStatusSm to avoid conflict with proto-generated aurore::GimbalStatus
struct GimbalStatusSm {
    float az_error_deg{999.f};
    float el_error_deg{999.f};
    float velocity_deg_s{999.f};
    int settled_frames{0};
};

struct TrackSolution {
    float centroid_x{0.f};
    float centroid_y{0.f};
    float velocity_x{0.f};
    float velocity_y{0.f};
    bool valid{false};
    float psr{0.f};
};

struct FireControlSolution {
    float az_lead_deg{0.f};
    float el_lead_deg{0.f};
    float range_m{0.f};
    float velocity_m_s{0.f};
    float p_hit{0.f};
    bool kinetic_mode{true};
};

struct alignas(64) BallisticsFrameState {
    TrackSolution track;
    FireControlSolution fc;
    PositionHistoryEntry pos_history[3];
    uint64_t last_update_ns{0};
    uint8_t stable_frames{0};
    bool solution_valid{false};
    uint8_t padding[6]{0};
};

static_assert(sizeof(BallisticsFrameState) <= 256, "BallisticsFrameState should fit in one cache line region");

// AM7-L3-SAFE-002: Range data structure with timestamp and checksum
struct RangeData {
    float range_m{0.f};           ///< Range value in meters
    uint64_t timestamp_ns{0};     ///< Capture timestamp (CLOCK_MONOTONIC_RAW)
    uint16_t checksum{0};         ///< CRC-16 checksum for validation

    // AM7-L3-SAFE-002: Valid range bounds [0.5m, 5000m]
    static constexpr float kRangeMinM = 0.5f;
    static constexpr float kRangeMaxM = 5000.0f;
    static constexpr uint64_t kMaxAgeNs = 100000000ULL;  // 100ms in nanoseconds
};

// AM7-L3-MODE-011: Fault codes for fault transition
enum class FaultCode : uint8_t {
    CAMERA_TIMEOUT = 0,
    GIMBAL_TIMEOUT = 1,
    RANGE_DATA_STALE = 2,
    RANGE_DATA_INVALID = 3,
    AUTH_FAILURE = 4,
    SEQUENCE_GAP = 5,
    TEMPERATURE_CRITICAL = 6,
    WATCHDOG_TIMEOUT = 7,
    I2C_FAULT = 8,
};

using StateChangeCb = std::function<void(FcsState from, FcsState to)>;

class StateMachine {
   public:
    StateMachine();

    FcsState state() const;
    void tick(std::chrono::milliseconds dt);

    void on_detection(const Detection& d);
    void on_tracker_initialized(const TrackSolution& sol);
    void on_tracker_update(const TrackSolution& sol);
    void on_gimbal_status(const GimbalStatusSm& g);
    void on_redetection_score(float score);
    void on_lrf_range(const RangeData& range);
    void on_ballistics_solution(const FireControlSolution& sol);
    void on_fire_command();

    void set_state_change_callback(StateChangeCb cb);
    void force_state_for_test(FcsState s);

    // AM7-L3-SAFE-002: Public CRC-16 computation for checksum generation
    static uint16_t compute_crc16(float range_m, uint64_t timestamp_ns) noexcept;

    // AM7-L3-MODE-011: Fault transition from any state
    void on_fault(FaultCode code);

    // AM7-L2-MODE-001: Hardware init complete signal (BOOT -> IDLE_SAFE)
    void on_init_complete();

    // AM7-L2-MODE-001: BOOT state initialization failure (BOOT -> FAULT)
    void on_boot_failure();

    // AM7-L3-MODE-006: Manual reset from FAULT state (FAULT -> IDLE_SAFE)
    void on_manual_reset();

    // AM7-L2-MODE-003, AM7-L2-MODE-004: Operator mode requests
    void request_freecam();
    void request_search();

    // AM7-L3-MODE-006: Operator cancel request (TRACKING/SEARCH/FREECAM -> IDLE_SAFE)
    void request_cancel();

    // AM7-L3-MODE-006: Disarm request (ARMED -> IDLE_SAFE)
    void request_disarm();

    // AM7-L3-MODE-010: ARMED entry conditions
    // INT-010: Call this method from external input source (e.g., ground station message,
    // physical safety interlock, or operator console). ARMED state is unreachable
    // without authorization per AM7-L3-MODE-010.
    void set_operator_authorization(bool authorized);

    // AM7-L3-MODE-007: Safety posture (interlock control)
    void set_interlock_enabled(bool enabled);
    bool is_interlock_enabled() const;

    // State query helpers
    bool has_valid_lock() const;
    bool has_stable_timing() const;
    bool has_zero_faults() const;
    bool has_operator_authorization() const;

   private:
    void transition(FcsState next);
    void enter_state(FcsState s);

    FcsState state_{FcsState::BOOT};  // AM7-L2-MODE-001: Start in BOOT state
    StateChangeCb on_change_;

    Detection first_detection_;
    bool have_first_detection_{false};
    std::chrono::milliseconds state_age_{0};

    // AM7-L2-MODE-004: SEARCH timeout
    static constexpr int kSearchTimeoutMs = 5000;  // 5s search timeout
    static constexpr int kArmedTimeoutMs = 100;    // 100ms armed timeout

    static constexpr float kSpatialGatePx = 50.f;
    static constexpr float kConfidenceMin = 0.95f;  // AM7-L2-TGT-003, AM7-L3-MODE-010

    static constexpr float kGimbalErrorMaxDeg = 2.f;
    static constexpr float kGimbalVelocityMaxDs = 5.f;
    static constexpr int kSettledFramesMin = 3;

    static constexpr float kRedetectionScoreMin = 0.95f;  // AM7-L3-MODE-010
    static constexpr float kPHitMin = 0.95f;
    static constexpr float kPsrFailThreshold = 0.035f;

    static constexpr float kAlignErrorMaxDeg = 0.5f;
    static constexpr int kAlignSustainMs = 20;

    GimbalStatusSm gimbal_{};
    FireControlSolution solution_{};
    float redetection_score_{0.f};
    int align_sustained_ms_{0};

    // AM7-L3-MODE-007: Safety posture (interlock)
    bool interlock_enabled_{false};

    // AM7-L2-MODE-007: FAULT state - latched fault
    bool fault_latched_{false};

    // AM7-L3-MODE-010: Operator authorization for ARMED
    bool operator_authorized_{false};

    // AM7-L3-SAFE-002: Range data validation state
    RangeData last_valid_range_{};
    bool have_valid_range_{false};
    std::chrono::milliseconds range_age_ms_{0};

    // AM7-L2-TGT-003/004: Target validation state
    // Position history for 3-frame stability validation (Δ ≤ 2 pixels)
    PositionHistoryEntry position_history_[3]{};
    int position_history_idx_{0};           // Circular buffer index
    int stable_frame_count_{0};             // Consecutive stable frames
    bool position_valid_{false};            // 3-frame stability achieved

    // AM7-L2-TGT-004: Lock confirmation state (250ms window, 95% stability)
    std::chrono::milliseconds lock_confirm_age_ms_{0};
    int lock_confirm_stable_frames_{0};     // Stable frames in 250ms window
    bool lock_confirmed_{false};            // 250ms lock confirmation achieved

    // AM7-L2-TGT-003: Stability threshold (Δ ≤ 2 pixels)
    static constexpr float kPositionStabilityPx = 2.0f;
    static constexpr int kStableFramesMin = 3;  // 3 consecutive stable frames

    // AM7-L2-TGT-004: Lock confirmation window (250ms = 30 frames at 120Hz)
    static constexpr int kLockConfirmWindowMs = 250;
    static constexpr float kLockConfirmThreshold = 0.95f;  // 95% stability

    // AM7-L2-TGT-003: Check if position is stable (Δ ≤ 2 pixels for 3 frames)
    bool is_position_stable() const noexcept;

    // AM7-L2-TGT-004: Update position history and stability counter
    void update_position_history(const Detection& d) noexcept;

    // AM7-L2-TGT-004: Update lock confirmation state
    void update_lock_confirmation(bool is_stable) noexcept;

    // AM7-L2-TGT-004: Reset validation state on state transition
    void reset_target_validation() noexcept;
};

const char* fcs_state_name(FcsState s);

}  // namespace aurore
