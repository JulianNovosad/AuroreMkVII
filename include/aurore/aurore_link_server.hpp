#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "state_machine.hpp"
#include "timing.hpp"

namespace aurore {

/**
 * @brief Operator input message structure (ICD-005)
 */
#pragma pack(push, 1)
struct LinkInputHeader {
    uint32_t sync_word;     ///< 0xAURORE05
    uint16_t message_id;    ///< Command type identifier
    uint32_t sequence;      ///< Monotonic sequence number
    uint64_t timestamp_ns;  ///< CLOCK_MONOTONIC_RAW nanoseconds
};

struct LinkInputMessage {
    LinkInputHeader header;
    uint8_t payload[32];
    uint8_t hmac[32];  ///< HMAC-SHA256 (32 bytes)
};

/**
 * @brief System response message structure (ICD-005)
 */
struct LinkOutputHeader {
    uint32_t sync_word;     ///< 0xAURORE06
    uint16_t message_id;    ///< Response type identifier
    uint32_t sequence;      ///< Matches request sequence
    uint64_t timestamp_ns;  ///< CLOCK_MONOTONIC_RAW nanoseconds
};

struct LinkOutputMessage {
    LinkOutputHeader header;
    uint8_t status;       ///< 0=ACK, 1=NACK, 2=PENDING
    uint8_t error_code;   ///< NACK reason code
    uint8_t payload[28];  ///< Response-specific data
    uint8_t hmac[32];     ///< HMAC-SHA256 (32 bytes)
};

/**
 * @brief ICD-005 Message IDs
 */
enum class LinkMsgId : uint16_t {
    // Operator -> System
    kModeRequest = 0x0101,
    kGimbalCommand = 0x0102,
    kZoomCommand = 0x0103,
    kTargetSelect = 0x0104,
    kTargetConfirm = 0x0105,  // Spec: ICD-005
    kTargetReject = 0x0106,   // Spec: ICD-005
    kArmRequest = 0x0107,
    kDisarmRequest = 0x0108,
    kEmergencyInhibit = 0x0109,
    kHeartbeat = 0x010A,

    // System -> Operator
    kModeAck = 0x0201,
    kModeNack = 0x0202,
    kSystemState = 0x0203,
    kTargetStatus = 0x0204,
    kFaultStatus = 0x0205
};

/**
 * @brief MODE_REQUEST payload (ICD-005)
 */
struct LinkPayloadModeRequest {
    uint8_t target_mode;  ///< 0=IDLE/SAFE, 1=FREECAM, 2=SEARCH, 3=TRACKING, 4=ARMED
    uint8_t reserved[31];
};

/**
 * @brief GIMBAL_COMMAND payload (ICD-005)
 */
struct LinkPayloadGimbalCmd {
    int16_t azimuth_rate;    ///< milliradians/sec * 100
    int16_t elevation_rate;  ///< milliradians/sec * 100
    uint8_t reserved[28];
};

/**
 * @brief SYSTEM_STATE payload (ICD-005)
 */
struct LinkPayloadSystemState {
    uint8_t current_mode;
    uint8_t interlock;
    uint8_t target_lock;
    uint8_t fault_active;
    uint8_t reserved[28];
};

/**
 * @brief TARGET_SELECT payload (ICD-005, line 571-575)
 */
struct LinkPayloadTargetSelect {
    uint16_t cursor_x;      ///< Pixel coordinates, 0-1536
    uint16_t cursor_y;      ///< Pixel coordinates, 0-864
    uint8_t confidence;     ///< Operator confidence 0-100
    uint8_t reserved[2];
};

/**
 * @brief TARGET_CONFIRM payload (ICD-005, line 577-578)
 */
struct LinkPayloadTargetConfirm {
    uint32_t target_id;     ///< Automatic target identifier
    uint8_t reserved[28];
};

/**
 * @brief TARGET_REJECT payload (ICD-005, line 580-583)
 */
struct LinkPayloadTargetReject {
    uint32_t target_id;     ///< Automatic target identifier
    uint8_t reason;          ///< 0-255 reason code
    uint8_t reserved[3];
};
#pragma pack(pop)

enum class LinkMode { AUTO = 0, FREECAM = 1 };

// Forward-declare generated protobuf types
class Telemetry;
class VideoFrame;
class Command;

struct AuroreLinkConfig {
    uint16_t telemetry_port = 9000;  // Telemetry TCP (MkVII → Link)
    uint16_t video_port = 9001;      // Video TCP (MkVII → Link)
    uint16_t command_port = 9002;    // Command TCP (Link → MkVII)
    size_t max_clients = 4;
    std::string hmac_key = "";  // HMAC-SHA256 key for command authentication
};

// Callbacks installed by main.cpp:
using ModeCallback = std::function<void(LinkMode)>;
using FreecamCallback = std::function<void(float az_deg, float el_deg, float velocity_dps)>;
using ArmCallback = std::function<void(bool authorized)>;
using HeartbeatTimeoutCallback = std::function<void()>;
using EmergencyStopCallback = std::function<void()>;
using TargetSelectCallback = std::function<void(uint16_t cursor_x, uint16_t cursor_y, uint8_t confidence)>;  // Spec: ICD-005
using TargetConfirmCallback = std::function<void(uint32_t target_id)>;  // Spec: ICD-005
using TargetRejectCallback = std::function<void(uint32_t target_id, uint8_t reason)>;  // Spec: ICD-005
// Spec: AM7-L3-SEC-001 - HMAC security event callback for logging authentication failures
using SecurityEventCallback = std::function<void(const std::string& event_type, uint32_t sequence)>;

/**
 * AuroreLinkServer - TCP server for remote operator interface
 *
 * Protocol: Fixed 64-byte binary structure with HMAC per ICD-005.
 * All sockets are non-blocking with 10ms poll interval.
 */
class AuroreLinkServer {
   public:
    explicit AuroreLinkServer(const AuroreLinkConfig& cfg = {});
    ~AuroreLinkServer();

    bool start();
    void stop();

    // Broadcast to all connected telemetry clients (thread-safe)
    void broadcast_telemetry(const Telemetry& msg);
    // Broadcast status response to all command clients
    void broadcast_status(const LinkPayloadSystemState& state);
    // Broadcast annotated video frame (thread-safe)
    void broadcast_video(const VideoFrame& frame);

    void set_mode_callback(ModeCallback cb);
    void set_freecam_callback(FreecamCallback cb);
    void set_arm_callback(ArmCallback cb);
    void set_heartbeat_timeout_callback(HeartbeatTimeoutCallback cb);
    void set_emergency_stop_callback(EmergencyStopCallback cb);
    void set_target_select_callback(TargetSelectCallback cb);   // Spec: ICD-005
    void set_target_confirm_callback(TargetConfirmCallback cb); // Spec: ICD-005
    void set_target_reject_callback(TargetRejectCallback cb);   // Spec: ICD-005
    void set_security_event_callback(SecurityEventCallback cb); // Spec: AM7-L3-SEC-001

    size_t client_count() const;
    LinkMode current_mode() const { return mode_.load(std::memory_order_acquire); }

   private:
    void telemetry_accept_loop();
    void video_accept_loop();
    void command_accept_loop();
    bool send_length_prefixed(int fd, const std::string& serialized);
    void handle_binary_command(int client_fd, const LinkInputMessage& msg);

    AuroreLinkConfig cfg_;
    std::atomic<bool> running_{false};
    std::atomic<LinkMode> mode_{LinkMode::AUTO};

    int telemetry_fd_{-1};
    int video_fd_{-1};
    int command_fd_{-1};

    std::thread telemetry_accept_thread_;
    std::thread video_accept_thread_;
    std::thread command_accept_thread_;

    mutable std::mutex clients_mutex_;
    std::vector<int> telemetry_clients_;
    std::vector<int> video_clients_;
    std::vector<int> command_clients_;

    ModeCallback on_mode_;
    FreecamCallback on_freecam_;
    ArmCallback on_arm_;
    HeartbeatTimeoutCallback on_heartbeat_timeout_;
    EmergencyStopCallback on_emergency_stop_;
    TargetSelectCallback on_target_select_;     // Spec: ICD-005
    TargetConfirmCallback on_target_confirm_;   // Spec: ICD-005
    TargetRejectCallback on_target_reject_;     // Spec: ICD-005
    SecurityEventCallback on_security_event_;   // Spec: AM7-L3-SEC-001

    // Spec: AM7-L3-SEC-001 - Send NACK for failed HMAC verification
    void send_nack(int client_fd, uint32_t sequence, uint16_t message_id, uint8_t error_code);

    // Heartbeat monitoring (500ms timeout → IDLE/SAFE)
    static constexpr uint64_t kHeartbeatTimeoutNs = 500000000ULL;  // 500ms
    std::atomic<TimestampNs> last_heartbeat_ns_{0};
    std::thread heartbeat_monitor_thread_;
    void heartbeat_monitor_loop();
};

}  // namespace aurore
