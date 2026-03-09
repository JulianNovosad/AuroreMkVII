#include "aurore/aurore_link_server.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
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
                struct timespec ts {
                    0, 10000000
                };  // 10ms
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
                struct timespec ts {
                    0, 10000000
                };  // 10ms
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
                struct timespec ts {
                    0, 10000000
                };
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
            while (running_.load(std::memory_order_acquire)) {
                LinkInputMessage msg{};
                ssize_t n = ::recv(client, &msg, sizeof(msg), MSG_WAITALL);
                if (n <= 0) break;
                if (n != sizeof(msg)) continue;

                if (msg.header.sync_word != 0xA7050005) {  // AURORE05 mnemonic -> 0xA7050005
                    std::cerr << "AuroreLink: Invalid sync word\n";
                    continue;
                }

                // SEC-001: Authenticate command via HMAC-SHA256 (ICD-005)
                if (!cfg_.hmac_key.empty()) {
                    // EMERGENCY_INHIBIT does not require authentication
                    if (msg.header.message_id !=
                        static_cast<uint16_t>(LinkMsgId::kEmergencyInhibit)) {
                        // Verification failure -> message discarded
                        if (!security::verify_hmac_sha256_raw(
                                cfg_.hmac_key, &msg, sizeof(LinkInputHeader) + 32, msg.hmac)) {
                            std::cerr << "AuroreLink: Authentication failure for msg 0x" << std::hex
                                      << msg.header.message_id << std::endl;
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
                            // TODO: Trigger security fault handler
                            // For now, log and continue
                        }
                        // Gap > 100: re-authentication required
                        else if (security::is_sequence_gap(old_seq, received_seq,
                                                           kSequenceGapReauthThreshold)) {
                            std::cerr
                                << "AuroreLink: Re-authentication required - sequence gap "
                                << (received_seq > old_seq ? received_seq - old_seq
                                                           : (1ULL << 32) - old_seq + received_seq)
                                << " exceeds threshold " << kSequenceGapReauthThreshold << "\n";
                            // TODO: Trigger re-authentication flow
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
        case LinkMsgId::kModeRequest: {
            LinkPayloadModeRequest payload;
            std::memcpy(&payload, msg.payload, sizeof(payload));
            if (on_mode_) {
                LinkMode m = (payload.target_mode == 3) ? LinkMode::AUTO : LinkMode::FREECAM;
                on_mode_(m);
            }
            break;
        }
        case LinkMsgId::kGimbalCommand: {
            LinkPayloadGimbalCmd payload;
            std::memcpy(&payload, msg.payload, sizeof(payload));
            if (on_freecam_) {
                // Convert milliradians/sec to degrees/sec for callback
                float az_dps = (static_cast<float>(payload.azimuth_rate) / 100.0f) * 0.0572958f;
                (void)az_dps;                     // Used by on_freecam_ callback
                on_freecam_(0.0f, 0.0f, az_dps);  // Placeholder, assuming rate-based for now
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
            // Update heartbeat timestamp
            last_heartbeat_ns_.store(aurore::get_timestamp(), std::memory_order_release);
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
        default:
            break;
    }
}

void AuroreLinkServer::broadcast_status(const LinkPayloadSystemState& state) {
    LinkOutputMessage msg{};
    msg.header.sync_word = 0xA7060006;  // AURORE06 mnemonic -> 0xA7060006
    msg.header.message_id = static_cast<uint16_t>(LinkMsgId::kSystemState);
    msg.header.timestamp_ns = 0;  // TODO
    msg.status = 0;               // ACK
    std::memcpy(msg.payload, &state, sizeof(state));

    if (!cfg_.hmac_key.empty()) {
        security::compute_hmac_sha256_raw(cfg_.hmac_key, &msg, sizeof(LinkOutputHeader) + 2 + 28,
                                          msg.hmac);
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

void AuroreLinkServer::heartbeat_monitor_loop() {
    // Heartbeat timeout monitor thread
    // Checks every 100ms for heartbeat timeout (500ms → IDLE/SAFE)
    constexpr uint64_t kCheckIntervalNs = 100000000ULL;  // 100ms
    struct timespec sleep_ts {};
    sleep_ts.tv_nsec = kCheckIntervalNs;

    while (running_.load(std::memory_order_acquire)) {
        clock_nanosleep(CLOCK_MONOTONIC, 0, &sleep_ts, nullptr);

        const TimestampNs now = aurore::get_timestamp();
        const TimestampNs last_hb = last_heartbeat_ns_.load(std::memory_order_acquire);
        const int64_t age_ns = timestamp_diff_ns(now, last_hb);

        // Check if heartbeat timeout exceeded
        if (static_cast<uint64_t>(age_ns) > kHeartbeatTimeoutNs) {
            std::cerr << "AuroreLink: HEARTBEAT TIMEOUT - " << (age_ns / 1000000)
                      << "ms since last heartbeat (threshold: 500ms)\n";
            if (on_heartbeat_timeout_) {
                on_heartbeat_timeout_();
            }
        }
    }
}

size_t AuroreLinkServer::client_count() const {
    std::lock_guard<std::mutex> lk(clients_mutex_);
    return telemetry_clients_.size();
}

}  // namespace aurore
