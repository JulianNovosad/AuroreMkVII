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
    BOOT = 0,        // AM7-L2-MODE-001: Hardware init, memory lock, self-test
    IDLE_SAFE = 1,   // AM7-L2-MODE-002: Inhibit, gimbal hold, no lock
    FREECAM = 2,     // AM7-L2-MODE-003: Manual gimbal control, no auto-lock
    SEARCH = 3,      // AM7-L2-MODE-004: Auto FOV scan, target acquisition
    TRACKING = 4,    // AM7-L2-MODE-005: Continuous target lock, gimbal active
    ARMED = 5,       // AM7-L2-MODE-006: Interlock enable permitted
    FAULT = 6,       // AM7-L2-MODE-007: Forced inhibit, gimbal hold
};

struct Detection {
    float confidence{0.0f};
    struct { int x, y, w, h; } bbox{};
    float cx() const { return static_cast<float>(bbox.x) + bbox.w * 0.5f; }
    float cy() const { return static_cast<float>(bbox.y) + bbox.h * 0.5f; }
};

struct GimbalStatus {
    float az_error_deg{999.f};
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
    void on_gimbal_status(const GimbalStatus& g);
    void on_redetection_score(float score);
    void on_lrf_range(float range_m);
    void on_ballistics_solution(const FireControlSolution& sol);
    void on_fire_command();

    void set_state_change_callback(StateChangeCb cb);
    void force_state_for_test(FcsState s);
    
    // AM7-L3-MODE-011: Fault transition from any state
    void on_fault(FaultCode code);
    
    // AM7-L2-MODE-003, AM7-L2-MODE-004: Operator mode requests
    void request_freecam();
    void request_search();
    
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
    static constexpr int kArmedTimeoutMs  = 100;   // 100ms armed timeout

    static constexpr float kSpatialGatePx = 50.f;
    static constexpr float kConfidenceMin = 0.7f;

    static constexpr float kGimbalErrorMaxDeg   = 2.f;
    static constexpr float kGimbalVelocityMaxDs = 5.f;
    static constexpr int   kSettledFramesMin    = 3;

    static constexpr float kRedetectionScoreMin = 0.85f;
    static constexpr float kPHitMin             = 0.95f;
    static constexpr float kPsrFailThreshold    = 0.035f;

    static constexpr float kAlignErrorMaxDeg = 0.5f;
    static constexpr int   kAlignSustainMs   =  20;

    GimbalStatus gimbal_{};
    FireControlSolution solution_{};
    float redetection_score_{0.f};
    int align_sustained_ms_{0};
    
    // AM7-L3-MODE-007: Safety posture (interlock)
    bool interlock_enabled_{false};
    
    // AM7-L2-MODE-007: FAULT state - latched fault
    bool fault_latched_{false};
    
    // AM7-L3-MODE-010: Operator authorization for ARMED
    bool operator_authorized_{false};
};

const char* fcs_state_name(FcsState s);

}  // namespace aurore
