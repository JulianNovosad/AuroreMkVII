#include "aurore/hud_socket.hpp"

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>

#include "aurore/security.hpp"
#include "aurore/state_machine.hpp"
#include "aurore/timing.hpp"

namespace aurore {

HudSocket::HudSocket(const HudSocketConfig& config)
    : socket_path_(config.socket_path),
      config_(config),
      tokens_(config.rate_limit_msgs_per_sec)  // Start with full bucket
      ,
      last_refill_ns_(get_timestamp()) {}  // Initialize refill timestamp

HudSocket::~HudSocket() { stop(); }

bool HudSocket::start() {
    // Clean up any existing socket file
    ::unlink(socket_path_.c_str());

    // ICD-006 specifies SOCK_SEQPACKET for message preservation
    server_fd_ = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (server_fd_ < 0) {
        std::cerr << "HudSocket: socket() failed: " << strerror(errno) << "\n";
        return false;
    }

    // Set non-blocking
    int flags = ::fcntl(server_fd_, F_GETFL, 0);
    if (flags < 0) {
        std::cerr << "HudSocket: fcntl(F_GETFL) failed: " << strerror(errno) << "\n";
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }
    ::fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK);

    // SEC-008: Set socket file permissions before bind
    // Create socket with restrictive permissions (0600 = owner read/write only)
    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(server_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "HudSocket: bind() failed: " << strerror(errno) << "\n";
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    // SEC-008: Explicitly set file permissions after bind
    if (::chmod(socket_path_.c_str(), config_.socket_permissions) != 0) {
        std::cerr << "HudSocket: chmod() failed: " << strerror(errno) << "\n";
        // Continue anyway - socket still works with default permissions
    }

    // SEC-008: Set listen backlog to max_clients to prevent queue overflow
    int backlog = static_cast<int>(config_.max_clients);
    if (::listen(server_fd_, backlog) < 0) {
        std::cerr << "HudSocket: listen() failed: " << strerror(errno) << "\n";
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    running_.store(true, std::memory_order_release);
    accept_thread_ = std::thread(&HudSocket::accept_loop, this);
    return true;
}

void HudSocket::stop() {
    running_.store(false, std::memory_order_release);

    // Wake up accept thread by closing server socket
    if (server_fd_ >= 0) {
        ::close(server_fd_);
        server_fd_ = -1;
    }

    // Wait for accept thread
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    // Close all client connections
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (int fd : client_fds_) {
        ::close(fd);
    }
    client_fds_.clear();

    // Clean up socket file
    ::unlink(socket_path_.c_str());
}

// SEC-008: Peer credential validation using SO_PEERCRED
SocketAuthStatus HudSocket::validate_peer_credentials(int client_fd) {
    struct ucred peer_cred {};
    socklen_t peer_cred_len = sizeof(peer_cred);

    // Get peer credentials
    if (::getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &peer_cred, &peer_cred_len) != 0) {
        std::cerr << "HudSocket: getsockopt(SO_PEERCRED) failed: " << strerror(errno) << "\n";
        return SocketAuthStatus::kCredentialError;
    }

    // SEC-008: Validate UID
    if (config_.require_root_uid && peer_cred.uid != config_.allowed_uid) {
        std::cerr << "HudSocket: connection rejected - UID " << peer_cred.uid
                  << " not authorized (requires " << config_.allowed_uid << ")\n";
        auth_failures_.fetch_add(1, std::memory_order_relaxed);
        return SocketAuthStatus::kUnauthorizedUid;
    }

    // SEC-008: Validate GID (optional, only if explicitly configured)
    if (config_.allowed_gid != 0 && peer_cred.gid != config_.allowed_gid) {
        std::cerr << "HudSocket: connection rejected - GID " << peer_cred.gid
                  << " not authorized (requires " << config_.allowed_gid << ")\n";
        auth_failures_.fetch_add(1, std::memory_order_relaxed);
        return SocketAuthStatus::kUnauthorizedGid;
    }

    return SocketAuthStatus::kOk;
}

void HudSocket::accept_loop() {
    while (running_.load(std::memory_order_acquire)) {
        struct sockaddr_un client_addr {};
        socklen_t client_addr_len = sizeof(client_addr);

        int client = ::accept(server_fd_, reinterpret_cast<struct sockaddr*>(&client_addr),
                              &client_addr_len);

        if (client < 0) {
            // EAGAIN/EWOULDBLOCK expected for non-blocking socket
            if (errno == EAGAIN || errno == EINTR) {
                // PERF-007: Use clock_nanosleep instead of sleep_for
                struct timespec ts {};
                ts.tv_sec = 0;
                ts.tv_nsec = 5000000;  // 5ms
                clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);
                continue;
            }
            std::cerr << "HudSocket: accept() failed: " << strerror(errno) << "\n";
            continue;
        }

        // SEC-008: Validate peer credentials before accepting connection
        SocketAuthStatus auth_status = validate_peer_credentials(client);
        if (auth_status != SocketAuthStatus::kOk) {
            connections_rejected_.fetch_add(1, std::memory_order_relaxed);
            ::close(client);
            continue;
        }

        // SEC-008: Check max clients limit (DoS prevention)
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            if (client_fds_.size() >= config_.max_clients) {
                std::cerr << "HudSocket: max clients (" << config_.max_clients
                          << ") exceeded, rejecting connection\n";
                connections_rejected_.fetch_add(1, std::memory_order_relaxed);
                auth_failures_.fetch_add(1, std::memory_order_relaxed);
                ::close(client);
                continue;
            }

            client_fds_.push_back(client);
        }

        connections_accepted_.fetch_add(1, std::memory_order_relaxed);
        std::cout << "HudSocket: client connected (total: " << get_client_count() << ")\n";
    }
}

void HudSocket::send_to_clients(const void* data, size_t size) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    std::vector<int> dead;

    for (int fd : client_fds_) {
        // ICD-006: SOCK_SEQPACKET send() preserves message boundaries
        ssize_t n = ::send(fd, data, size, MSG_NOSIGNAL);
        if (n < 0) {
            dead.push_back(fd);
        }
    }

    // Remove dead connections
    if (!dead.empty()) {
        for (int fd : dead) {
            ::close(fd);
        }

        client_fds_.erase(
            std::remove_if(
                client_fds_.begin(), client_fds_.end(),
                [&dead](int fd) { return std::find(dead.begin(), dead.end(), fd) != dead.end(); }),
            client_fds_.end());
    }
}

size_t HudSocket::get_client_count() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return client_fds_.size();
}

// PERF-008: Token bucket rate limiter implementation
// Refills tokens at configured rate, consumes one token per message
bool HudSocket::try_acquire_token() {
    const uint64_t now_ns = get_timestamp();
    const uint64_t last_ns = last_refill_ns_.load(std::memory_order_acquire);

    // Calculate elapsed time and refill tokens
    const double max_tokens = config_.rate_limit_msgs_per_sec;
    const double refill_rate = max_tokens;  // tokens per second

    // Time elapsed since last refill (in seconds)
    const double elapsed_sec = static_cast<double>(now_ns - last_ns) / 1e9;

    // Refill tokens based on elapsed time
    double tokens = tokens_.load(std::memory_order_acquire);
    tokens = std::min(max_tokens, tokens + elapsed_sec * refill_rate);

    // Try to consume a token
    if (tokens >= 1.0) {
        tokens -= 1.0;
        tokens_.store(tokens, std::memory_order_release);
        last_refill_ns_.store(now_ns, std::memory_order_release);
        return true;  // Token acquired, message allowed
    }

    // Update refill timestamp even when rate limited (prevents token starvation)
    last_refill_ns_.store(now_ns, std::memory_order_release);
    return false;  // No token available, rate limited
}

// PERF-008: Message timestamp validation
// Discards messages older than configured timeout
bool HudSocket::is_message_fresh(uint64_t timestamp_ns) const {
    const uint64_t now_ns = get_timestamp();
    const uint64_t max_age_ns = static_cast<uint64_t>(config_.message_timeout_ms * 1e6);

    // Check for timestamp in the future (clock skew protection)
    if (timestamp_ns > now_ns) {
        // Allow small future timestamps (up to 10ms) for clock skew
        if (timestamp_ns - now_ns > 10'000'000ULL) {
            std::cerr << "HudSocket: message timestamp " << timestamp_ns
                      << " is too far in the future (now: " << now_ns << ")\n";
            return false;
        }
        return true;  // Small skew is acceptable
    }

    // Check message age
    const uint64_t age_ns = now_ns - timestamp_ns;
    if (age_ns > max_age_ns) {
        return false;  // Message too old, discard
    }

    return true;  // Message is fresh
}

void HudSocket::broadcast(const HudFrame& frame) {
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }

    // PERF-008: Validate message timestamp (discard stale messages)
    if (!is_message_fresh(frame.timestamp_ns)) {
        timeout_discarded_count_.fetch_add(1, std::memory_order_relaxed);
        std::cerr << "HudSocket: discarding stale message (age > " << config_.message_timeout_ms
                  << "ms)\n";
        return;
    }

    // PERF-008: Apply rate limiting (token bucket)
    if (!try_acquire_token()) {
        rate_limited_count_.fetch_add(1, std::memory_order_relaxed);
        std::cerr << "HudSocket: rate limit exceeded (>" << config_.rate_limit_msgs_per_sec
                  << " msg/sec)\n";
        return;
    }

    static uint32_t sequence = 0;
    sequence++;

    // Prepare binary messages per ICD-006
    auto prepare_msg = [&](HudMsgId id, const void* payload) {
        HudBinaryMessage msg{};
        msg.header.sync_word = 0xA7070007;  // AURORE07 mnemonic -> 0xA7070007
        msg.header.message_id = static_cast<uint16_t>(id);
        msg.header.sequence = sequence;
        msg.header.timestamp_ns = frame.timestamp_ns;
        std::memcpy(msg.payload, payload, 32);

        // Compute HMAC over header + payload
        if (!config_.hmac_key.empty()) {
            security::compute_hmac_sha256_raw(config_.hmac_key, &msg, sizeof(HudBinaryHeader) + 32,
                                              msg.hmac);
        }
        return msg;
    };

    // 1. RETICLE_DATA
    HudPayloadReticle reticle{};
    reticle.reticle_x = static_cast<int16_t>(frame.az_deg * 100);  // Placeholder mapping
    reticle.reticle_y = static_cast<int16_t>(frame.el_deg * 100);
    reticle.lead_offset_x = static_cast<int16_t>(frame.az_lead_mrad * 100);
    reticle.lead_offset_y = static_cast<int16_t>(frame.el_lead_mrad * 100);
    HudBinaryMessage msg_reticle = prepare_msg(HudMsgId::kReticleData, &reticle);
    send_to_clients(&msg_reticle, sizeof(msg_reticle));

    // 2. TARGET_BOX
    HudPayloadTargetBox box{};
    box.box_x = static_cast<uint16_t>(frame.target_cx - frame.target_w / 2);
    box.box_y = static_cast<uint16_t>(frame.target_cy - frame.target_h / 2);
    box.box_width = static_cast<uint16_t>(frame.target_w);
    box.box_height = static_cast<uint16_t>(frame.target_h);
    box.confidence = static_cast<uint8_t>(frame.confidence * 100);
    HudBinaryMessage msg_box = prepare_msg(HudMsgId::kTargetBox, &box);
    send_to_clients(&msg_box, sizeof(msg_box));

    // 3. BALLISTIC_SOLUTION
    HudPayloadBallistics ball{};
    ball.elevation_adj = static_cast<int16_t>(frame.el_lead_mrad * 100);
    ball.azimuth_adj = static_cast<int16_t>(frame.az_lead_mrad * 100);
    ball.range_m = static_cast<uint16_t>(frame.range_m);
    ball.ammo_id = frame.ammo_id;
    HudBinaryMessage msg_ball = prepare_msg(HudMsgId::kBallisticSolution, &ball);
    send_to_clients(&msg_ball, sizeof(msg_ball));

    // 4. SYSTEM_STATUS
    HudPayloadStatus status{};
    status.fcs_state = frame.state;
    status.interlock = frame.interlock;
    status.target_lock = frame.target_lock;
    status.fault_active = frame.fault_active;
    status.cpu_temp_c = frame.cpu_temp_c;
    status.deadline_misses = static_cast<uint16_t>(frame.deadline_misses);
    HudBinaryMessage msg_status = prepare_msg(HudMsgId::kSystemStatus, &status);
    send_to_clients(&msg_status, sizeof(msg_status));
}

}  // namespace aurore
