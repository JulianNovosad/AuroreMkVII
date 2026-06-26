#include "aurore/aurore_link_server.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <unordered_map>

#include "aurore.pb.h"
#include "aurore/security.hpp"

namespace aurore {

// Per-client sequence number tracking for replay attack prevention
static std::unordered_map<int, uint32_t> client_sequences_;
static std::mutex client_sequences_mutex_;

// Security thresholds
static constexpr uint32_t kSequenceGapReauthThreshold = 100;  // Gap > 100: re-auth required
static constexpr uint32_t kSequenceGapFaultThreshold = 1000;  // Gap > 1000: security fault

// Spec: AM7-L3-SEC-001 - NACK error codes for HMAC authentication failures
static constexpr uint8_t kNackErrInvalidHmac = 0x01;     // HMAC verification failed
static constexpr uint8_t kNackErrReplayDetected = 0x02;  // Replay attack detected
static constexpr uint8_t kNackErrSequenceGap = 0x03;     // Sequence gap too large

AuroreLinkServer::AuroreLinkServer(const AuroreLinkConfig& cfg) : cfg_(cfg) {}

AuroreLinkServer::~AuroreLinkServer() { stop(); }

static int make_tcp_listen_socket(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }
    ::listen(fd, 8);
    return fd;
}

bool AuroreLinkServer::start() {
    telemetry_fd_ = make_tcp_listen_socket(cfg_.telemetry_port);
    video_fd_ = make_tcp_listen_socket(cfg_.video_port);
    command_fd_ = make_tcp_listen_socket(cfg_.command_port);
    if (telemetry_fd_ < 0 || video_fd_ < 0 || command_fd_ < 0) {
        std::cerr << "AuroreLink: failed to bind ports\n";
        return false;
    }
    running_.store(true, std::memory_order_release);
    // Initialize heartbeat timestamp to current time (prevents immediate timeout on start)
    last_heartbeat_ns_.store(aurore::get_timestamp(), std::memory_order_release);
    telemetry_accept_thread_ = std::thread(&AuroreLinkServer::telemetry_accept_loop, this);
    video_accept_thread_ = std::thread(&AuroreLinkServer::video_accept_loop, this);
    command_accept_thread_ = std::thread(&AuroreLinkServer::command_accept_loop, this);
    heartbeat_monitor_thread_ = std::thread(&AuroreLinkServer::heartbeat_monitor_loop, this);
    link_monitor_thread_ = std::thread(&AuroreLinkServer::link_monitor_loop, this);
    std::cout << "AuroreLink listening: telemetry=" << cfg_.telemetry_port
              << " video=" << cfg_.video_port << " command=" << cfg_.command_port << "\n";
    return true;
}

void AuroreLinkServer::stop() {
    running_.store(false, std::memory_order_release);
    if (telemetry_fd_ >= 0) {
        ::close(telemetry_fd_);
        telemetry_fd_ = -1;
    }
    if (video_fd_ >= 0) {
        ::close(video_fd_);
        video_fd_ = -1;
    }
    if (command_fd_ >= 0) {
        ::close(command_fd_);
        command_fd_ = -1;
    }
    if (telemetry_accept_thread_.joinable()) telemetry_accept_thread_.join();
    if (video_accept_thread_.joinable()) video_accept_thread_.join();
    if (command_accept_thread_.joinable()) command_accept_thread_.join();
    if (heartbeat_monitor_thread_.joinable()) heartbeat_monitor_thread_.join();
    if (link_monitor_thread_.joinable()) link_monitor_thread_.join();
    std::lock_guard<std::mutex> lk(clients_mutex_);
    for (int fd : telemetry_clients_) ::close(fd);
    for (int fd : video_clients_) ::close(fd);
    for (int fd : command_clients_) ::close(fd);
    telemetry_clients_.clear();
    video_clients_.clear();
    command_clients_.clear();

    // Clean up all client sequence tracking
    {
        std::lock_guard<std::mutex> seq_lk(client_sequences_mutex_);
        client_sequences_.clear();
    }
}

bool AuroreLinkServer::send_length_prefixed(int fd, const std::string& data) {
    uint32_t net_len = htonl(static_cast<uint32_t>(data.size()));
    if (::send(fd, &net_len, 4, MSG_NOSIGNAL) != 4) return false;
    ssize_t sent = ::send(fd, data.data(), data.size(), MSG_NOSIGNAL);
    return sent == static_cast<ssize_t>(data.size());
}

void AuroreLinkServer::broadcast_telemetry(const Telemetry& msg) {
    std::string data;
    if (!msg.SerializeToString(&data)) return;
    std::lock_guard<std::mutex> lk(clients_mutex_);
    std::vector<int> dead;
    for (int fd : telemetry_clients_) {
        if (!send_length_prefixed(fd, data)) {
            dead.push_back(fd);
        }
    }
    for (int fd : dead) {
        ::close(fd);
    }
    telemetry_clients_.erase(
        std::remove_if(
            telemetry_clients_.begin(), telemetry_clients_.end(),
            [&dead](int fd) { return std::find(dead.begin(), dead.end(), fd) != dead.end(); }),
        telemetry_clients_.end());
}

void AuroreLinkServer::broadcast_video(const VideoFrame& frame) {
    std::string data;
    if (!frame.SerializeToString(&data)) return;
    std::lock_guard<std::mutex> lk(clients_mutex_);
    std::vector<int> dead;
    for (int fd : video_clients_) {
        if (!send_length_prefixed(fd, data)) {
            dead.push_back(fd);
        }
    }
    for (int fd : dead) {
        ::close(fd);
    }
    video_clients_.erase(
        std::remove_if(
            video_clients_.begin(), video_clients_.end(),
            [&dead](int fd) { return std::find(dead.begin(), dead.end(), fd) != dead.end(); }),
        video_clients_.end());
}

void AuroreLinkServer::telemetry_accept_loop() {
    while (running_.load(std::memory_order_acquire)) {
        int client = ::accept(telemetry_fd_, nullptr, nullptr);
        if (client < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                struct timespec ts{0, 10000000};  // 10ms
                clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);
                continue;
            }
            break;
        }
        std::lock_guard<std::mutex> lk(clients_mutex_);
        if (telemetry_clients_.size() < cfg_.max_clients) {
            telemetry_clients_.push_back(client);
            std::cout << "AuroreLink: telemetry client connected (" << telemetry_clients_.size()
                      << "/" << cfg_.max_clients << ")\n";
        } else {
            ::close(client);
        }
    }
}

void AuroreLinkServer::video_accept_loop() {
    while (running_.load(std::memory_order_acquire)) {
        int client = ::accept(video_fd_, nullptr, nullptr);
        if (client < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                struct timespec ts{0, 10000000};  // 10ms
                clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);
                continue;
            }
            break;
        }
        std::lock_guard<std::mutex> lk(clients_mutex_);
        if (video_clients_.size() < cfg_.max_clients) {
            video_clients_.push_back(client);
            std::cout << "AuroreLink: video client connected (" << video_clients_.size() << "/"
                      << cfg_.max_clients << ")\n";
        } else {
            ::close(client);
        }
    }
}

void AuroreLinkServer::command_accept_loop() {
    while (running_.load(std::memory_order_acquire)) {
        int client = ::accept(command_fd_, nullptr, nullptr);
        if (client < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                struct timespec ts{0, 10000000};
                clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);
                continue;
            }
            break;
        }

        {
            std::lock_guard<std::mutex> lk(clients_mutex_);
            if (command_clients_.size() >= cfg_.max_clients) {
                ::close(client);
                continue;
            }
            command_clients_.push_back(client);
        }

        // Spawn detached reader thread per command client
        std::thread([this, client]() {
            // AM7-L2-SEC-005: session timeout — close idle command connections after N seconds
            if (cfg_.session_timeout_s > 0) {
                struct timeval tv;
                tv.tv_sec = static_cast<time_t>(cfg_.session_timeout_s);
                tv.tv_usec = 0;
                ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            }

            // AM7-L3-IF-003: Per-client token bucket — max 120 msg/sec, drop newest on overflow
            static constexpr uint64_t kWindowNs = 1000000000ULL;  // 1-second sliding window
            uint64_t window_start_ns = aurore::get_timestamp();
            int window_count = 0;
            const int kMaxPerWindow = static_cast<int>(kMaxCommandRateHz);

            while (running_.load(std::memory_order_acquire)) {
                LinkInputMessage msg{};
                ssize_t n = ::recv(client, &msg, sizeof(msg), MSG_WAITALL);
                if (n <= 0) {
                    if (n < 0 && errno == EAGAIN) {
                        // Session idle timeout — log and disconnect
                        std::cerr << "AuroreLink: session timeout after " << cfg_.session_timeout_s
                                  << "s idle — disconnecting client fd=" << client << "\n";
                        if (on_security_event_) on_security_event_("SESSION_TIMEOUT", 0);
                    }
                    break;
                }
                if (n != sizeof(msg)) continue;

                // AM7-L3-IF-003: Enforce rate limit — drop newest on overflow, assert warning
                {
                    uint64_t now_ns = aurore::get_timestamp();
                    if (now_ns - window_start_ns >= kWindowNs) {
                        window_start_ns = now_ns;
                        window_count = 0;
                    }
                    if (window_count >= kMaxPerWindow) {
                        overflow_count_.fetch_add(1, std::memory_order_relaxed);
                        std::cerr << "AuroreLink: WARN input buffer overflow — dropping message"
                                  << " (AM7-L3-IF-003, overflow #"
                                  << overflow_count_.load(std::memory_order_relaxed) << ")\n";
                        continue;  // drop newest
                    }
                    ++window_count;
                }

                if (msg.header.sync_word != 0xA7050005) {  // AURORE05 mnemonic -> 0xA7050005
                    std::cerr << "AuroreLink: Invalid sync word\n";
                    continue;
                }

                // SEC-001: Authenticate command via HMAC-SHA256 (ICD-005)
                // Spec: AM7-L2-SEC-001 - HMAC-SHA256 with 256-bit keys
                // Spec: AM7-L3-SEC-001 - HMAC over message + sequence + timestamp
                if (!cfg_.hmac_key.empty()) {
                    // EMERGENCY_INHIBIT does not require authentication
                    if (msg.header.message_id !=
                        static_cast<uint16_t>(LinkMsgId::kEmergencyInhibit)) {
                        // Verification failure -> send NACK and log event
                        if (!security::verify_hmac_sha256_raw(cfg_.hmac_key, &msg,
                                                              sizeof(LinkInputHeader) + 32,
                                                              msg.hmac.data())) {
                            std::cerr << "AuroreLink: HMAC verification failed for msg 0x"
                                      << std::hex << msg.header.message_id << " seq=" << std::dec
                                      << msg.header.sequence << std::endl;

                            // Spec: AM7-L3-SEC-001 - Log security event
                            if (on_security_event_) {
                                on_security_event_("HMAC_VERIFY_FAIL", msg.header.sequence);
                            }

                            // Spec: ICD-005 - Return NACK for invalid HMAC
                            send_nack(client, msg.header.sequence, msg.header.message_id,
                                      kNackErrInvalidHmac);
                            continue;
                        }
                    }
                }

                // SEC-010: Validate sequence number (replay attack prevention)
                {
                    std::lock_guard<std::mutex> lk(client_sequences_mutex_);
                    uint32_t expected_seq = 0;
                    auto it = client_sequences_.find(client);
                    if (it != client_sequences_.end()) {
                        expected_seq = it->second + 1;
                    }

                    uint32_t received_seq = msg.header.sequence;

                    // Verify sequence number (RFC 1982 wrap-aware comparison)
                    if (!security::verify_sequence_number(received_seq, expected_seq)) {
                        std::cerr << "AuroreLink: Replay attack detected - seq " << received_seq
                                  << " < expected " << expected_seq << "\n";

                        // Spec: AM7-L3-SEC-001 - Log security event for replay attack
                        if (on_security_event_) {
                            on_security_event_("REPLAY_ATTACK", received_seq);
                        }

                        // Spec: ICD-005 - Return NACK for replay attack
                        send_nack(client, msg.header.sequence, msg.header.message_id,
                                  kNackErrReplayDetected);
                        continue;  // Discard replayed message
                    }

                    // Check for sequence gap (packet loss or attack)
                    if (it != client_sequences_.end()) {
                        uint32_t old_seq = it->second;

                        // Gap > 1000: security fault
                        if (security::is_sequence_gap(old_seq, received_seq,
                                                      kSequenceGapFaultThreshold)) {
                            std::cerr
                                << "AuroreLink: SECURITY FAULT - sequence gap "
                                << (received_seq > old_seq ? received_seq - old_seq
                                                           : (1ULL << 32) - old_seq + received_seq)
                                << " exceeds threshold " << kSequenceGapFaultThreshold << "\n";

                            // Spec: AM7-L3-SEC-001 - Log security fault event
                            if (on_security_event_) {
                                on_security_event_("SEQ_GAP_FAULT", received_seq);
                            }
                        }
                        // Gap > 100: re-authentication required
                        else if (security::is_sequence_gap(old_seq, received_seq,
                                                           kSequenceGapReauthThreshold)) {
                            std::cerr
                                << "AuroreLink: Re-authentication required - sequence gap "
                                << (received_seq > old_seq ? received_seq - old_seq
                                                           : (1ULL << 32) - old_seq + received_seq)
                                << " exceeds threshold " << kSequenceGapReauthThreshold << "\n";

                            // Spec: AM7-L3-SEC-001 - Log re-auth required event
                            if (on_security_event_) {
                                on_security_event_("REAUTH_REQUIRED", received_seq);
                            }

                            // Spec: ICD-005 - Return NACK for sequence gap
                            send_nack(client, msg.header.sequence, msg.header.message_id,
                                      kNackErrSequenceGap);
                            continue;
                        }
                    }

                    // Update expected sequence number
                    client_sequences_[client] = received_seq;
                }

                handle_binary_command(client, msg);
            }
            ::close(client);

            // Clean up client sequence tracking
            {
                std::lock_guard<std::mutex> lk(client_sequences_mutex_);
                client_sequences_.erase(client);
            }

            std::lock_guard<std::mutex> lk(clients_mutex_);
            command_clients_.erase(
                std::remove(command_clients_.begin(), command_clients_.end(), client),
                command_clients_.end());
        }).detach();
    }
}

void AuroreLinkServer::handle_binary_command(int client_fd, const LinkInputMessage& msg) {
    (void)client_fd;
    LinkMsgId id = static_cast<LinkMsgId>(msg.header.message_id);

    switch (id) {
        case LinkMsgId::kZoomCommand: {
            // AM7-L2-IF-004: Scroll wheel zoom (digital ROI crop or optical if equipped)
            LinkPayloadZoomCmd payload;
            std::memcpy(&payload, msg.payload.data(), sizeof(payload));
            if (on_zoom_) {
                on_zoom_(payload.zoom_direction, payload.zoom_rate);
            }
            break;
        }
        case LinkMsgId::kModeRequest: {
            LinkPayloadModeRequest payload;
            std::memcpy(&payload, msg.payload.data(), sizeof(payload));
            if (on_mode_) {
                LinkMode m = (payload.target_mode == 3) ? LinkMode::AUTO : LinkMode::FREECAM;
                on_mode_(m);
            }
            break;
        }
        case LinkMsgId::kGimbalCommand: {
            LinkPayloadGimbalCmd payload;
            std::memcpy(&payload, msg.payload.data(), sizeof(payload));
            if (on_freecam_) {
                // Convert milliradians/sec * 100 to degrees/sec
                float az_dps = (static_cast<float>(payload.azimuth_rate) / 100.0f) * 0.0572958f;
                float el_dps = (static_cast<float>(payload.elevation_rate) / 100.0f) * 0.0572958f;
                // AM7-L3-IF-002: Log out-of-range gimbal rates (physical limit 60°/s)
                constexpr float kMaxRateDps = 60.0f;
                if (std::abs(az_dps) > kMaxRateDps || std::abs(el_dps) > kMaxRateDps) {
                    std::cerr << "AuroreLink: WARN gimbal rate out of range"
                              << " az=" << az_dps << " el=" << el_dps << " dps (max " << kMaxRateDps
                              << " dps) — clamped by controller\n";
                }
                on_freecam_(az_dps, el_dps, 0.0f, msg.header.sequence);
            }
            break;
        }
        case LinkMsgId::kArmRequest: {
            if (on_arm_) on_arm_(true);
            break;
        }
        case LinkMsgId::kDisarmRequest: {
            if (on_arm_) on_arm_(false);
            break;
        }
        case LinkMsgId::kHeartbeat: {
            // Update heartbeat timestamp and clear timeout edge-detect flag
            last_heartbeat_ns_.store(aurore::get_timestamp(), std::memory_order_release);
            heartbeat_timed_out_.store(false, std::memory_order_release);
            break;
        }
        case LinkMsgId::kEmergencyInhibit: {
            // EMERGENCY_INHIBIT (0x0109) - immediate FAULT state transition
            // Per spec: No authentication required, immediate action
            std::cerr << "AuroreLink: EMERGENCY_INHIBIT received - triggering emergency stop\n";
            if (on_emergency_stop_) {
                on_emergency_stop_();
            }
            break;
        }
        // Spec: ICD-005 - Target selection commands
        case LinkMsgId::kTargetSelect: {
            LinkPayloadTargetSelect payload;
            std::memcpy(&payload, msg.payload.data(), sizeof(payload));
            if (on_target_select_) {
                on_target_select_(payload.cursor_x, payload.cursor_y, payload.confidence);
            }
            break;
        }
        case LinkMsgId::kTargetConfirm: {
            LinkPayloadTargetConfirm payload;
            std::memcpy(&payload, msg.payload.data(), sizeof(payload));
            if (on_target_confirm_) {
                on_target_confirm_(payload.target_id);
            }
            break;
        }
        case LinkMsgId::kTargetReject: {
            LinkPayloadTargetReject payload;
            std::memcpy(&payload, msg.payload.data(), sizeof(payload));
            if (on_target_reject_) {
                on_target_reject_(payload.target_id, payload.reason);
            }
            break;
        }
        default:
            // AM7-L3-IF-002: Log unknown/unsupported message IDs as warnings
            std::cerr << "AuroreLink: WARN unknown message_id=0x" << std::hex
                      << msg.header.message_id << std::dec << " seq=" << msg.header.sequence
                      << " — ignored\n";
            break;
    }
}

void AuroreLinkServer::broadcast_status(const LinkPayloadSystemState& state) {
    LinkOutputMessage msg{};
    msg.header.sync_word = 0xA7060006;  // AURORE06 mnemonic -> 0xA7060006
    msg.header.message_id = static_cast<uint16_t>(LinkMsgId::kSystemState);
    msg.header.timestamp_ns = aurore::get_timestamp();
    msg.status = 0;  // ACK
    std::memcpy(msg.payload.data(), &state, sizeof(state));

    if (!cfg_.hmac_key.empty()) {
        security::compute_hmac_sha256_raw(cfg_.hmac_key, &msg, sizeof(LinkOutputHeader) + 2 + 28,
                                          msg.hmac.data());
    }

    std::lock_guard<std::mutex> lk(clients_mutex_);
    for (int fd : command_clients_) {
        ::send(fd, &msg, sizeof(msg), MSG_NOSIGNAL);
    }
}

void AuroreLinkServer::set_mode_callback(ModeCallback cb) { on_mode_ = std::move(cb); }

void AuroreLinkServer::set_freecam_callback(FreecamCallback cb) { on_freecam_ = std::move(cb); }

void AuroreLinkServer::set_arm_callback(ArmCallback cb) { on_arm_ = std::move(cb); }

void AuroreLinkServer::set_heartbeat_timeout_callback(HeartbeatTimeoutCallback cb) {
    on_heartbeat_timeout_ = std::move(cb);
}

void AuroreLinkServer::set_emergency_stop_callback(EmergencyStopCallback cb) {
    on_emergency_stop_ = std::move(cb);
}

// Spec: AM7-L3-SEC-001 - Send NACK response for failed authentication
void AuroreLinkServer::send_nack(int client_fd, uint32_t sequence, uint16_t message_id,
                                 uint8_t error_code) {
    (void)message_id;  // Reserved for future use (e.g., to specify which message failed)

    LinkOutputMessage msg{};
    msg.header.sync_word = 0xA7060006;  // AURORE06
    msg.header.message_id =
        static_cast<uint16_t>(LinkMsgId::kModeNack);  // Use ModeNack for error response
    msg.header.sequence = sequence;
    msg.header.timestamp_ns = aurore::get_timestamp();
    msg.status = 1;  // NACK
    msg.error_code = error_code;

    // HMAC generation for outgoing response
    // Spec: AM7-L2-SEC-001 - HMAC-SHA256 with 256-bit keys
    if (!cfg_.hmac_key.empty()) {
        security::compute_hmac_sha256_raw(cfg_.hmac_key, &msg, sizeof(LinkOutputHeader) + 2 + 28,
                                          msg.hmac.data());
    }

    ::send(client_fd, &msg, sizeof(msg), MSG_NOSIGNAL);
}

void AuroreLinkServer::set_security_event_callback(SecurityEventCallback cb) {
    on_security_event_ = std::move(cb);
}

// Spec: ICD-005 - Target selection callbacks
void AuroreLinkServer::set_target_select_callback(TargetSelectCallback cb) {
    on_target_select_ = std::move(cb);
}

void AuroreLinkServer::set_target_confirm_callback(TargetConfirmCallback cb) {
    on_target_confirm_ = std::move(cb);
}

void AuroreLinkServer::set_target_reject_callback(TargetRejectCallback cb) {
    on_target_reject_ = std::move(cb);
}

void AuroreLinkServer::set_zoom_callback(ZoomCallback cb) { on_zoom_ = std::move(cb); }

void AuroreLinkServer::heartbeat_monitor_loop() {
    // Heartbeat timeout monitor thread
    // Checks every 100ms for heartbeat timeout (1000ms → IDLE/SAFE)
    constexpr uint64_t kCheckIntervalNs = 100000000ULL;  // 100ms
    struct timespec sleep_ts{};
    sleep_ts.tv_nsec = kCheckIntervalNs;

    while (running_.load(std::memory_order_acquire)) {
        clock_nanosleep(CLOCK_MONOTONIC, 0, &sleep_ts, nullptr);

        const TimestampNs now = aurore::get_timestamp();
        const TimestampNs last_hb = last_heartbeat_ns_.load(std::memory_order_acquire);
        const int64_t age_ns = timestamp_diff_ns(now, last_hb);

        // Check if heartbeat timeout exceeded — fire callback only on the rising edge
        if (static_cast<uint64_t>(age_ns) > kHeartbeatTimeoutNs) {
            if (!heartbeat_timed_out_.exchange(true, std::memory_order_acq_rel)) {
                std::cerr << "AuroreLink: HEARTBEAT TIMEOUT - " << (age_ns / 1000000)
                          << "ms since last heartbeat (threshold: 1000ms)\n";
                if (on_heartbeat_timeout_) {
                    on_heartbeat_timeout_();
                }
            }
        } else {
            heartbeat_timed_out_.store(false, std::memory_order_release);
        }
    }
}

/**
 * AM7-L3-IF-001/005: Ethernet link monitor
 *
 * Polls /sys/class/net/<iface>/operstate every 80ms (12.5 Hz ≥ 10 Hz spec).
 * On link-down, immediately fires the heartbeat timeout callback to trigger
 * IDLE/SAFE transition within one poll interval (≤ 80ms ≤ 100ms spec).
 */
void AuroreLinkServer::link_monitor_loop() {
    const std::string operstate_path = "/sys/class/net/" + cfg_.ethernet_interface + "/operstate";

    struct timespec sleep_ts{};
    sleep_ts.tv_nsec = static_cast<long>(kLinkPollIntervalNs);

    bool was_down = false;

    while (running_.load(std::memory_order_acquire)) {
        clock_nanosleep(CLOCK_MONOTONIC, 0, &sleep_ts, nullptr);

        std::ifstream operstate(operstate_path);
        if (!operstate.is_open()) {
            continue;  // File absent (e.g. native build without eth0) — skip silently
        }

        std::string state;
        operstate >> state;

        const bool link_down =
            (state == "down" || state == "lowerlayerdown" || state == "notpresent");

        if (link_down && !was_down) {
            std::cerr << "AuroreLink: Ethernet link " << cfg_.ethernet_interface
                      << " DOWN — triggering IDLE/SAFE (AM7-L3-IF-001)\n";
            // Reuse heartbeat timeout callback: link loss = operator disconnected
            if (on_heartbeat_timeout_) {
                on_heartbeat_timeout_();
            }
        }
        was_down = link_down;
    }
}

size_t AuroreLinkServer::client_count() const {
    std::lock_guard<std::mutex> lk(clients_mutex_);
    return telemetry_clients_.size();
}

}  // namespace aurore
