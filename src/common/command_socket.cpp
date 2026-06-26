#include "aurore/command_socket.hpp"

#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>

namespace aurore {

CommandSocket::CommandSocket() : cfg_(Config{}) {}
CommandSocket::CommandSocket(const Config& cfg) : cfg_(cfg) {}

CommandSocket::~CommandSocket() { stop(); }

void CommandSocket::set_mode_callback(ModeCallback cb) {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    on_mode_ = std::move(cb);
}

void CommandSocket::set_freecam_callback(FreecamCallback cb) {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    on_freecam_ = std::move(cb);
}

void CommandSocket::set_reset_callback(ResetCallback cb) {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    on_reset_ = std::move(cb);
}

bool CommandSocket::start() {
    // Remove stale socket file
    ::unlink(cfg_.socket_path.c_str());

    server_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "[CommandSocket] socket(): " << strerror(errno) << std::endl;
        return false;
    }

    // Non-blocking accept loop
    ::fcntl(server_fd_, F_SETFL, O_NONBLOCK);

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, cfg_.socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(server_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[CommandSocket] bind(): " << strerror(errno) << std::endl;
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    if (::listen(server_fd_, 4) < 0) {
        std::cerr << "[CommandSocket] listen(): " << strerror(errno) << std::endl;
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    // World-readable so Node.js (non-root user) can connect
    ::chmod(cfg_.socket_path.c_str(), 0777);

    running_.store(true, std::memory_order_release);
    accept_thread_ = std::thread(&CommandSocket::accept_loop, this);

    std::cout << "[CommandSocket] Listening on " << cfg_.socket_path << std::endl;
    return true;
}

void CommandSocket::stop() {
    running_.store(false, std::memory_order_release);

    if (server_fd_ >= 0) {
        ::shutdown(server_fd_, SHUT_RDWR);
        ::close(server_fd_);
        server_fd_ = -1;
        ::unlink(cfg_.socket_path.c_str());
    }

    // Close all client connections
    {
        std::lock_guard<std::mutex> lk(clients_mutex_);
        for (int fd : client_fds_) {
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
        }
        client_fds_.clear();
    }

    if (accept_thread_.joinable()) accept_thread_.join();
}

void CommandSocket::accept_loop() {
    while (running_.load(std::memory_order_acquire)) {
        struct pollfd pfd{server_fd_, POLLIN, 0};
        int ret = ::poll(&pfd, 1, 200);  // 200ms timeout → check running_ flag
        if (ret <= 0) continue;

        struct sockaddr_un client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd =
            ::accept(server_fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &len);
        if (client_fd < 0) continue;

        // SEC: SO_PEERCRED validation — reject unauthorized processes
        if (cfg_.allowed_uid != 0) {
            struct ucred peer{};
            socklen_t peer_len = sizeof(peer);
            const bool cred_ok =
                (::getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &peer, &peer_len) == 0);
            if (!cred_ok || peer.uid != cfg_.allowed_uid) {
                std::cerr << "[CommandSocket] rejected connection: UID "
                          << (cred_ok ? std::to_string(peer.uid) : "unknown") << " != allowed "
                          << cfg_.allowed_uid << "\n";
                ::close(client_fd);
                continue;
            }
        }

        {
            std::lock_guard<std::mutex> lk(clients_mutex_);
            client_fds_.push_back(client_fd);
        }

        std::cout << "[CommandSocket] Client connected (fd=" << client_fd << ")" << std::endl;

        // Spawn per-client thread
        std::thread([this, client_fd]() {
            client_loop(client_fd);
            std::lock_guard<std::mutex> lk(clients_mutex_);
            client_fds_.erase(std::remove(client_fds_.begin(), client_fds_.end(), client_fd),
                              client_fds_.end());
            ::close(client_fd);
            std::cout << "[CommandSocket] Client disconnected (fd=" << client_fd << ")"
                      << std::endl;
        }).detach();
    }
}

void CommandSocket::client_loop(int fd) {
    std::string buf;
    char tmp[256];

    while (running_.load(std::memory_order_acquire)) {
        struct pollfd pfd{fd, POLLIN, 0};
        int ret = ::poll(&pfd, 1, 200);
        if (ret < 0) break;
        if (ret == 0) continue;
        if (pfd.revents & (POLLHUP | POLLERR)) break;

        ssize_t n = ::read(fd, tmp, sizeof(tmp) - 1);
        if (n <= 0) break;

        buf.append(tmp, static_cast<size_t>(n));

        // Process all complete lines
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            // Strip carriage return
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) dispatch(line);
        }

        // Safety limit: discard if buffer grows too large
        if (buf.size() > 1024) buf.clear();
    }
}

void CommandSocket::dispatch(const std::string& line) {
    std::lock_guard<std::mutex> lk(cb_mutex_);

    if (line.rfind("MODE ", 0) == 0) {
        std::string mode = line.substr(5);
        if (on_mode_) on_mode_(mode);
        return;
    }

    if (line.rfind("FREECAM ", 0) == 0) {
        std::istringstream ss(line.substr(8));
        float az = 0.f, el = 0.f;
        if (ss >> az >> el) {
            if (on_freecam_) on_freecam_(az, el);
        }
        return;
    }

    if (line == "RESET") {
        if (on_reset_) on_reset_();
        return;
    }

    std::cerr << "[CommandSocket] Unknown command: " << line << std::endl;
}

}  // namespace aurore
