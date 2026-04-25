#include "aurore/mjpeg_streamer.hpp"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace aurore {

MjpegStreamer::MjpegStreamer(const std::string& socket_path, int input_width, int input_height)
    : socket_path_(socket_path), input_width_(input_width), input_height_(input_height) {}

MjpegStreamer::~MjpegStreamer() {
    stop();
}

bool MjpegStreamer::start() {
    // Pre-allocate staging buffer at full input resolution.
    // Resize to stream resolution happens in encode_loop (non-RT thread).
    staging_frame_.create(input_height_, input_width_, CV_8UC3);

    // Ensure socket directory exists
    auto sock_dir = std::filesystem::path(socket_path_).parent_path();
    if (!sock_dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(sock_dir, ec);
    }

    // Remove stale socket file
    ::unlink(socket_path_.c_str());

    server_fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server_fd_ < 0) {
        std::cerr << "[MjpegStreamer] socket() failed: " << std::strerror(errno) << '\n';
        return false;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[MjpegStreamer] bind() failed: " << std::strerror(errno) << '\n';
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    ::chmod(socket_path_.c_str(), 0666);  // world-readable so pi user can connect

    if (::listen(server_fd_, 8) < 0) {
        std::cerr << "[MjpegStreamer] listen() failed: " << std::strerror(errno) << '\n';
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    running_.store(true, std::memory_order_release);
    accept_thread_ = std::thread(&MjpegStreamer::accept_loop, this);
    encode_thread_ = std::thread(&MjpegStreamer::encode_loop, this);

    std::cout << "[MjpegStreamer] Listening on " << socket_path_ << '\n';
    return true;
}

void MjpegStreamer::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;

    if (server_fd_ >= 0) {
        ::shutdown(server_fd_, SHUT_RDWR);
        ::close(server_fd_);
        server_fd_ = -1;
    }

    {
        std::lock_guard<std::mutex> lk(clients_mtx_);
        for (int fd : clients_) ::close(fd);
        clients_.clear();
    }

    if (accept_thread_.joinable()) accept_thread_.join();
    if (encode_thread_.joinable()) encode_thread_.join();

    ::unlink(socket_path_.c_str());
}

void MjpegStreamer::push_frame(const cv::Mat& bgr_frame) {
    if (!running_.load(std::memory_order_acquire)) return;
    if (!has_clients()) return;

    // Non-blocking: drop frame if encode thread holds the mutex.
    // Plain copyTo — no resize here; encode thread handles INTER_AREA downscale.
    if (staging_mtx_.try_lock()) {
        bgr_frame.copyTo(staging_frame_);
        ++staging_seq_;
        staging_mtx_.unlock();
    }
}

bool MjpegStreamer::has_clients() const {
    std::lock_guard<std::mutex> lk(clients_mtx_);
    return !clients_.empty();
}

void MjpegStreamer::accept_loop() {
    while (running_.load(std::memory_order_acquire)) {
        int client_fd = ::accept(server_fd_, nullptr, nullptr);
        if (client_fd < 0) {
            if (running_.load(std::memory_order_acquire)) {
                std::cerr << "[MjpegStreamer] accept() error: " << std::strerror(errno) << '\n';
            }
            break;
        }
        std::lock_guard<std::mutex> lk(clients_mtx_);
        clients_.push_back(client_fd);
        std::cout << "[MjpegStreamer] Client connected (total: " << clients_.size() << ")\n";
    }
}

void MjpegStreamer::encode_loop() {
    // JPEG params: quality 85 (high), default subsampling
    std::vector<int> jpeg_params = {cv::IMWRITE_JPEG_QUALITY, 85};
    std::vector<uchar> jpeg_buf;
    cv::Mat local_frame;                                         // matches input resolution
    cv::Mat stream_frame(kStreamHeight, kStreamWidth, CV_8UC3);  // pre-allocated stream size

    while (running_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kEncodeIntervalMs));

        if (!has_clients()) continue;

        // Grab latest full-res staging frame
        {
            std::lock_guard<std::mutex> lk(staging_mtx_);
            if (staging_seq_ == last_encoded_seq_) continue;  // no new frame
            last_encoded_seq_ = staging_seq_;
            staging_frame_.copyTo(local_frame);
        }

        if (local_frame.empty()) continue;

        // Downscale on the non-RT encode thread — INTER_AREA is high quality but slow
        cv::resize(local_frame, stream_frame,
                   cv::Size(kStreamWidth, kStreamHeight),
                   0.0, 0.0, cv::INTER_AREA);

        cv::imencode(".jpg", stream_frame, jpeg_buf, jpeg_params);
        broadcast(jpeg_buf);
    }
}

void MjpegStreamer::broadcast(const std::vector<uchar>& jpeg) {
    // Wire format: 4-byte big-endian length + JPEG bytes
    uint32_t len = static_cast<uint32_t>(jpeg.size());
    uint8_t hdr[4] = {
        static_cast<uint8_t>(len >> 24),
        static_cast<uint8_t>(len >> 16),
        static_cast<uint8_t>(len >>  8),
        static_cast<uint8_t>(len),
    };

    std::vector<int> dead;

    {
        std::lock_guard<std::mutex> lk(clients_mtx_);
        for (int fd : clients_) {
            if (::send(fd, hdr,        4,           MSG_NOSIGNAL) < 0 ||
                ::send(fd, jpeg.data(), jpeg.size(), MSG_NOSIGNAL) < 0) {
                dead.push_back(fd);
            }
        }
    }

    for (int fd : dead) remove_client(fd);
}

void MjpegStreamer::remove_client(int fd) {
    std::lock_guard<std::mutex> lk(clients_mtx_);
    auto it = std::find(clients_.begin(), clients_.end(), fd);
    if (it != clients_.end()) {
        ::close(fd);
        clients_.erase(it);
        std::cout << "[MjpegStreamer] Client disconnected (remaining: " << clients_.size() << ")\n";
    }
}

}  // namespace aurore
