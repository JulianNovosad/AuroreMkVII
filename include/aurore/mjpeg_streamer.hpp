#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/core.hpp>

namespace aurore {

/**
 * Serves MIPI camera preview frames to aurore-link over a UNIX domain socket.
 *
 * Protocol: each frame is preceded by a 4-byte big-endian length, followed by
 * JPEG-encoded bytes.  The server.js /stream/mipi endpoint reads this socket and
 * wraps the frames in multipart/x-mixed-replace MJPEG for the browser.
 *
 * RT safety: push_frame() uses try_lock — it drops the frame silently if the
 * encode thread is busy, so the RT track thread is never blocked.
 */
class MjpegStreamer {
public:
    static constexpr const char* kDefaultSocketPath = "/run/aurore/mjpeg_stream.sock";
    static constexpr int kStreamWidth  = 1280;
    static constexpr int kStreamHeight = 720;
    static constexpr int kJpegQuality  = 75;
    // Absolute-timer encode interval: actual FPS = 1000/kEncodeIntervalMs as long as
    // encode completes within the interval.  16ms → up to 62fps.
    static constexpr int kEncodeIntervalMs = 16;

    // input_width/height: expected resolution of frames passed to push_frame().
    // Staging buffer is pre-allocated at this size so push_frame() never heap-allocates.
explicit MjpegStreamer(const std::string& socket_path = kDefaultSocketPath,
                            int input_width = 1280, int input_height = 720);
    ~MjpegStreamer();

    bool start();
    void stop();

    /**
     * Non-blocking push from the RT track thread.
     * Copies bgr_frame into the staging buffer (no resize — encode thread handles that).
     * Drops the frame silently if the encode thread holds the mutex.
     */
    void push_frame(const cv::Mat& bgr_frame);

    bool has_clients() const;

private:
    void accept_loop();
    void encode_loop();
    void broadcast(const std::vector<uchar>& jpeg);
    void remove_client(int fd);

    std::string socket_path_;
    int input_width_;
    int input_height_;
    int server_fd_{-1};

    std::atomic<bool> running_{false};

    // Staging buffer shared between RT thread (writer) and encode thread (reader).
    // Stored at full input resolution; encode thread resizes to stream resolution.
    std::mutex   staging_mtx_;
    cv::Mat      staging_frame_;   // pre-allocated input_height_ × input_width_ BGR
    uint64_t     staging_seq_{0};
    uint64_t     last_encoded_seq_{0};

    // Connected client file descriptors
    mutable std::mutex    clients_mtx_;
    std::vector<int>      clients_;

    std::thread accept_thread_;
    std::thread encode_thread_;
};

}  // namespace aurore
