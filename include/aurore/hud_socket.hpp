#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "state_machine.hpp"

namespace aurore {

/**
 * @brief HUD binary message header (ICD-006)
 */
#pragma pack(push, 1)
struct HudBinaryHeader {
    uint32_t sync_word;     ///< 0xAURORE07
    uint16_t message_id;    ///< Telemetry type identifier
    uint32_t sequence;      ///< Monotonic sequence number
    uint64_t timestamp_ns;  ///< CLOCK_MONOTONIC_RAW nanoseconds
};

/**
 * @brief HUD binary message structure (64 bytes total)
 */
struct HudBinaryMessage {
    HudBinaryHeader header;
    uint8_t payload[32];
    uint8_t hmac[32];  ///< HMAC-SHA256 (32 bytes)
};

/**
 * @brief ICD-006 Message IDs
 */
enum class HudMsgId : uint16_t {
    kReticleData = 0x0301,
    kTargetBox = 0x0302,
    kBallisticSolution = 0x0303,
    kSystemStatus = 0x0304
};

/**
 * @brief RETICLE_DATA payload (ICD-006)
 */
struct HudPayloadReticle {
    int16_t reticle_x;
    int16_t reticle_y;
    int16_t lead_offset_x;  ///< milliradians * 100
    int16_t lead_offset_y;  ///< milliradians * 100
    uint8_t reserved[24];
};

/**
 * @brief TARGET_BOX payload (ICD-006)
 */
struct HudPayloadTargetBox {
    uint16_t box_x;
    uint16_t box_y;
    uint16_t box_width;
    uint16_t box_height;
    uint8_t confidence;  ///< 0-100
    uint8_t reserved[27];
};

/**
 * @brief BALLISTIC_SOLUTION payload (ICD-006)
 */
struct HudPayloadBallistics {
    int16_t elevation_adj;  ///< milliradians * 100
    int16_t azimuth_adj;    ///< milliradians * 100
    uint16_t range_m;
    uint8_t ammo_id;
    uint8_t reserved[25];
};

/**
 * @brief SYSTEM_STATUS payload (ICD-006)
 */
struct HudPayloadStatus {
    uint8_t fcs_state;     ///< FcsState enum (0=BOOT, 1=IDLE/SAFE, etc.)
    uint8_t interlock;     ///< 0=inhibit, 1=enable
    uint8_t target_lock;   ///< 0=no lock, 1=locked
    uint8_t fault_active;  ///< 0=clear, 1=active
    uint16_t cpu_temp_c;   ///< CPU temp * 10 (0-1000 = 0°C to 100°C)
    uint16_t deadline_misses;
    uint8_t reserved[24];  ///< Padding to 32 bytes total
};
#pragma pack(pop)

struct HudFrame {
    uint8_t state{0};
    float az_deg{0.f};
    float el_deg{0.f};
    float target_cx{0.f};
    float target_cy{0.f};
    float confidence{0.f};
    float p_hit{0.f};
    float range_m{0.f};
    uint64_t timestamp_ns{0};
    float target_w{0.f};
    float target_h{0.f};
    float velocity_x{0.f};
    float velocity_y{0.f};
    float az_lead_mrad{0.f};
    float el_lead_mrad{0.f};
    uint32_t deadline_misses{0};
    uint8_t ammo_id{0};
    // SYSTEM_STATUS fields (AM7-L2-HUD-004)
    uint8_t interlock{0};       // 0=inhibit, 1=enable
    uint8_t target_lock{0};     // 0=no lock, 1=locked
    uint8_t fault_active{0};    // 0=clear, 1=active
    uint16_t cpu_temp_c{0};     // CPU temp * 10 (0-1000 = 0°C to 100°C)
};

// SEC-008: Peer credential validation result
enum class SocketAuthStatus : uint8_t {
    kOk = 0,
    kCredentialError = 1,
    kUnauthorizedUid = 2,
    kUnauthorizedGid = 3,
    kMaxClientsExceeded = 4
};

// SEC-008: Socket configuration
struct HudSocketConfig {
    std::string socket_path = "/run/aurore/hud_telemetry.sock";
    mode_t socket_permissions = 0660;  // SEC-008: Default per spec
    bool require_root_uid = true;      // Only root can connect
    uid_t allowed_uid = 0;             // Default: UID 0 (root)
    gid_t allowed_gid = 0;             // Default: GID 0 (root)
    size_t max_clients = 10;           // SEC-008: Prevent DoS via connection exhaustion
    std::string hmac_key;              ///< 32-byte secret key for telemetry authentication

    // Rate limiting configuration
    double rate_limit_msgs_per_sec = 120.0;  // Max messages per second
    double message_timeout_ms = 100.0;       // Max message age before discard
};

// UNIX domain socket server. Broadcasts newline-delimited JSON HUD frames
// to all connected clients (spec AM7-L2-HUD-004).
// SEC-008: Added authentication and access control
// PERF-008: Added rate limiting (120 msg/sec) and message timeout (>100ms discard)
class HudSocket {
   public:
    explicit HudSocket(const HudSocketConfig& config = HudSocketConfig());
    ~HudSocket();

    bool start();
    void stop();

    void broadcast(const HudFrame& frame);

    // SEC-008: Statistics
    size_t get_client_count() const;
    size_t get_max_clients() const { return config_.max_clients; }
    uint64_t get_auth_failures() const { return auth_failures_.load(std::memory_order_acquire); }

    // PERF-008: Rate limiting statistics
    uint64_t get_rate_limited_count() const { return rate_limited_count_.load(std::memory_order_acquire); }
    uint64_t get_timeout_discarded_count() const { return timeout_discarded_count_.load(std::memory_order_acquire); }

   private:
    void accept_loop();
    void send_to_clients(const void* data, size_t size);
    std::string frame_to_json(const HudFrame& f) const;

    // SEC-008: Peer credential validation
    SocketAuthStatus validate_peer_credentials(int client_fd);

    // PERF-008: Token bucket rate limiter
    bool try_acquire_token();
    // PERF-008: Message timestamp validation
    bool is_message_fresh(uint64_t timestamp_ns) const;

    std::string socket_path_;
    HudSocketConfig config_;
    int server_fd_{-1};
    std::atomic<bool> running_{false};
    std::thread accept_thread_;

    mutable std::mutex clients_mutex_;  // SEC-008: mutable for const methods
    std::vector<int> client_fds_;

    // SEC-008: Statistics
    std::atomic<uint64_t> auth_failures_{0};
    std::atomic<uint64_t> connections_accepted_{0};
    std::atomic<uint64_t> connections_rejected_{0};

    // PERF-008: Rate limiter state (token bucket algorithm)
    std::atomic<double> tokens_{120.0};  // Start with full bucket
    std::atomic<uint64_t> last_refill_ns_{0};  // Last token refill timestamp
    std::atomic<uint64_t> rate_limited_count_{0};  // Count of rate-limited messages
    std::atomic<uint64_t> timeout_discarded_count_{0};  // Count of stale messages discarded
};

}  // namespace aurore
