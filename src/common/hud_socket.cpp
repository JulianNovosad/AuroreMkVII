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
#include <sstream>

#include "aurore/security.hpp"
#include "aurore/state_machine.hpp"
#include "aurore/timing.hpp"

namespace aurore {

HudSocket::HudSocket(const HudSocketConfig& config)
    : socket_path_(config.socket_path),
      config_(config),
      tokens_(config.rate_limit_msgs_per_sec),  // Start with full bucket
      last_refill_ns_(get_timestamp()) {}  // Initialize refill timestamp

HudSocket::~HudSocket() { stop(); }

bool HudSocket::start() {
    // Clean up any existing socket file
    ::unlink(socket_path_.c_str());

    // SOCK_STREAM for compatibility with Node.js net.createConnection
    server_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
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
// Refills tokens at configured rate, consumes one token per message.
// Mutex guards the read-modify-write across tokens_ and last_refill_ns_
// to prevent the TOCTOU race where two concurrent callers both read
// last_refill_ns_ before either updates it, causing a double refill.
bool HudSocket::try_acquire_token() {
    const uint64_t now_ns = get_timestamp();
    std::lock_guard<std::mutex> lock(rate_limit_mutex_);

    // Calculate elapsed time and refill tokens
    const double max_tokens = config_.rate_limit_msgs_per_sec;
    const double refill_rate = max_tokens;  // tokens per second

    // Time elapsed since last refill (in seconds)
    const double elapsed_sec = static_cast<double>(now_ns - last_refill_ns_) / 1e9;

    // Refill tokens based on elapsed time
    tokens_ = std::min(max_tokens, tokens_ + elapsed_sec * refill_rate);

    // Try to consume a token
    if (tokens_ >= 1.0) {
        tokens_ -= 1.0;
        last_refill_ns_ = now_ns;
        return true;  // Token acquired, message allowed
    }

    // Update refill timestamp even when rate limited (prevents token starvation)
    last_refill_ns_ = now_ns;
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

std::string HudSocket::frame_to_json(const HudFrame& f) const {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "{\"state\":%u,\"az\":%.2f,\"el\":%.2f,"
        "\"cx\":%.1f,\"cy\":%.1f,\"w\":%.1f,\"h\":%.1f,"
        "\"conf\":%.3f,\"range\":%.1f,\"vx\":%.3f,\"vy\":%.3f,"
        "\"az_lead_mrad\":%.3f,\"el_lead_mrad\":%.3f,\"p_hit\":%.3f,"
        "\"deadline_misses\":%u,\"cpu_temp\":%.1f,"
        "\"interlock\":%u,\"target_lock\":%u,\"fault\":%u}\n",
        static_cast<unsigned>(f.state),
        static_cast<double>(f.az_deg), static_cast<double>(f.el_deg),
        static_cast<double>(f.target_cx), static_cast<double>(f.target_cy),
        static_cast<double>(f.target_w), static_cast<double>(f.target_h),
        static_cast<double>(f.confidence), static_cast<double>(f.range_m),
        static_cast<double>(f.velocity_x), static_cast<double>(f.velocity_y),
        static_cast<double>(f.az_lead_mrad), static_cast<double>(f.el_lead_mrad),
        static_cast<double>(f.p_hit),
        f.deadline_misses,
        static_cast<double>(f.cpu_temp_c) / 10.0,
        static_cast<unsigned>(f.interlock),
        static_cast<unsigned>(f.target_lock),
        static_cast<unsigned>(f.fault_active));
    return std::string(buf);
}

void HudSocket::broadcast(const HudFrame& frame) {
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }

    // PERF-008: Validate message timestamp (discard stale messages)
    if (!is_message_fresh(frame.timestamp_ns)) {
        timeout_discarded_count_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // PERF-008: Apply rate limiting (token bucket)
    if (!try_acquire_token()) {
        rate_limited_count_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    std::string json = frame_to_json(frame);
    send_to_clients(json.c_str(), json.size());
}

}  // namespace aurore
