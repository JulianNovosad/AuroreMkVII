#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace aurore {

struct HudFrame {
    uint8_t  state{0};
    float    az_deg{0.f};
    float    el_deg{0.f};
    float    target_cx{0.f};
    float    target_cy{0.f};
    float    confidence{0.f};
    float    p_hit{0.f};
    float    range_m{0.f};
    uint64_t timestamp_ns{0};
    float    target_w{0.f};
    float    target_h{0.f};
    float    velocity_x{0.f};
    float    velocity_y{0.f};
    float    az_lead_mrad{0.f};
    float    el_lead_mrad{0.f};
    uint32_t deadline_misses{0};
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
    std::string socket_path = "/tmp/aurore_hud.sock";
    mode_t socket_permissions = 0600;  // SEC-008: Default restrictive
    bool require_root_uid = true;      // Only root can connect
    uid_t allowed_uid = 0;             // Default: UID 0 (root)
    gid_t allowed_gid = 0;             // Default: GID 0 (root)
    size_t max_clients = 10;           // SEC-008: Prevent DoS via connection exhaustion
};

// UNIX domain socket server. Broadcasts newline-delimited JSON HUD frames
// to all connected clients (spec AM7-L2-HUD-004).
// SEC-008: Added authentication and access control
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
    uint64_t get_auth_failures() const {
        return auth_failures_.load(std::memory_order_acquire);
    }

private:
    void accept_loop();
    void send_to_clients(const std::string& msg);
    std::string frame_to_json(const HudFrame& f) const;

    // SEC-008: Peer credential validation
    SocketAuthStatus validate_peer_credentials(int client_fd);

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
};

}  // namespace aurore
