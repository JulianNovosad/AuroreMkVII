#include "aurore/aurore_link_server.hpp"
#include "aurore.pb.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>

namespace aurore {

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
        ::close(fd); return -1;
    }
    ::listen(fd, 8);
    return fd;
}

bool AuroreLinkServer::start() {
    telemetry_fd_ = make_tcp_listen_socket(cfg_.telemetry_port);
    video_fd_     = make_tcp_listen_socket(cfg_.video_port);
    command_fd_   = make_tcp_listen_socket(cfg_.command_port);
    if (telemetry_fd_ < 0 || video_fd_ < 0 || command_fd_ < 0) {
        std::cerr << "AuroreLink: failed to bind ports\n";
        return false;
    }
    running_.store(true, std::memory_order_release);
    telemetry_accept_thread_ = std::thread(&AuroreLinkServer::telemetry_accept_loop, this);
    video_accept_thread_     = std::thread(&AuroreLinkServer::video_accept_loop, this);
    command_accept_thread_   = std::thread(&AuroreLinkServer::command_accept_loop, this);
    std::cout << "AuroreLink listening: telemetry=" << cfg_.telemetry_port
              << " video=" << cfg_.video_port
              << " command=" << cfg_.command_port << "\n";
    return true;
}

void AuroreLinkServer::stop() {
    running_.store(false, std::memory_order_release);
    if (telemetry_fd_ >= 0) { ::close(telemetry_fd_); telemetry_fd_ = -1; }
    if (video_fd_   >= 0) { ::close(video_fd_);   video_fd_   = -1; }
    if (command_fd_   >= 0) { ::close(command_fd_);   command_fd_   = -1; }
    if (telemetry_accept_thread_.joinable()) telemetry_accept_thread_.join();
    if (video_accept_thread_.joinable())     video_accept_thread_.join();
    if (command_accept_thread_.joinable())   command_accept_thread_.join();
    std::lock_guard<std::mutex> lk(clients_mutex_);
    for (int fd : telemetry_clients_) ::close(fd);
    for (int fd : video_clients_)     ::close(fd);
    for (int fd : command_clients_)   ::close(fd);
    telemetry_clients_.clear();
    video_clients_.clear();
    command_clients_.clear();
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
    for (int fd : dead) { ::close(fd); }
    telemetry_clients_.erase(
        std::remove_if(telemetry_clients_.begin(), telemetry_clients_.end(),
            [&dead](int fd){ return std::find(dead.begin(),dead.end(),fd)!=dead.end(); }),
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
    for (int fd : dead) { ::close(fd); }
    video_clients_.erase(
        std::remove_if(video_clients_.begin(), video_clients_.end(),
            [&dead](int fd){ return std::find(dead.begin(),dead.end(),fd)!=dead.end(); }),
        video_clients_.end());
}

void AuroreLinkServer::telemetry_accept_loop() {
    while (running_.load(std::memory_order_acquire)) {
        int client = ::accept(telemetry_fd_, nullptr, nullptr);
        if (client < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                struct timespec ts{0, 10000000};  // 10ms
                clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);
                continue;
            }
            break;
        }
        std::lock_guard<std::mutex> lk(clients_mutex_);
        if (telemetry_clients_.size() < cfg_.max_clients) {
            telemetry_clients_.push_back(client);
            std::cout << "AuroreLink: telemetry client connected (" 
                      << telemetry_clients_.size() << "/" << cfg_.max_clients << ")\n";
        } else {
            ::close(client);
        }
    }
}

void AuroreLinkServer::video_accept_loop() {
    while (running_.load(std::memory_order_acquire)) {
        int client = ::accept(video_fd_, nullptr, nullptr);
        if (client < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                struct timespec ts{0, 10000000};  // 10ms
                clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);
                continue;
            }
            break;
        }
        std::lock_guard<std::mutex> lk(clients_mutex_);
        if (video_clients_.size() < cfg_.max_clients) {
            video_clients_.push_back(client);
            std::cout << "AuroreLink: video client connected (" 
                      << video_clients_.size() << "/" << cfg_.max_clients << ")\n";
        } else {
            ::close(client);
        }
    }
}

void AuroreLinkServer::command_accept_loop() {
    while (running_.load(std::memory_order_acquire)) {
        int client = ::accept(command_fd_, nullptr, nullptr);
        if (client < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                struct timespec ts{0, 10000000};
                clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);
                continue;
            }
            break;
        }
        // Spawn detached reader thread per command client
        std::thread([this, client]() {
            while (running_.load(std::memory_order_acquire)) {
                uint32_t net_len = 0;
                ssize_t n = ::recv(client, &net_len, 4, MSG_WAITALL);
                if (n != 4) break;
                uint32_t len = ntohl(net_len);
                if (len == 0 || len > 65536) break;
                std::string buf(len, '\0');
                n = ::recv(client, buf.data(), len, MSG_WAITALL);
                if (n != static_cast<ssize_t>(len)) break;
                Command cmd;
                if (!cmd.ParseFromString(buf)) continue;
                if (cmd.has_mode_switch() && on_mode_) {
                    LinkMode m = (cmd.mode_switch().mode() == ::aurore::AUTO) 
                                 ? LinkMode::AUTO : LinkMode::FREECAM;
                    on_mode_(m);
                }
                if (cmd.has_freecam() && on_freecam_) {
                    on_freecam_(
                        cmd.freecam().az_deg(),
                        cmd.freecam().el_deg(),
                        cmd.freecam().velocity_dps()
                    );
                }
            }
            ::close(client);
            std::lock_guard<std::mutex> lk(clients_mutex_);
            command_clients_.erase(
                std::remove(command_clients_.begin(), command_clients_.end(), client),
                command_clients_.end());
        }).detach();
    }
}

void AuroreLinkServer::set_mode_callback(ModeCallback cb) {
    on_mode_ = std::move(cb);
}

void AuroreLinkServer::set_freecam_callback(FreecamCallback cb) {
    on_freecam_ = std::move(cb);
}

size_t AuroreLinkServer::client_count() const {
    std::lock_guard<std::mutex> lk(clients_mutex_);
    return telemetry_clients_.size();
}

}  // namespace aurore
