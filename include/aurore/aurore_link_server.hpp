#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <cstdint>

// Forward-declare generated protobuf types
namespace aurore { class Telemetry; class VideoFrame; class Command; }

namespace aurore {

enum class LinkMode { AUTO = 0, FREECAM = 1 };

// Use protobuf-generated FreecamTarget instead of defining our own
// struct FreecamTarget { ... };  // Defined in aurore.pb.h

struct AuroreLinkConfig {
    uint16_t telemetry_port  = 9000;  // Telemetry TCP (MkVII → Link)
    uint16_t video_port      = 9001;  // Video TCP (MkVII → Link)
    uint16_t command_port    = 9002;  // Command TCP (Link → MkVII)
    size_t   max_clients     = 4;
};

// Callbacks installed by main.cpp:
using ModeCallback    = std::function<void(LinkMode)>;
using FreecamCallback = std::function<void(float az_deg, float el_deg, float velocity_dps)>;

/**
 * AuroreLinkServer - TCP server for remote operator interface
 * 
 * Three ports:
 * - telemetry_port (9000): Broadcast Telemetry protobuf messages (length-prefixed)
 * - video_port (9001): Broadcast VideoFrame JPEG messages (length-prefixed)
 * - command_port (9002): Accept Command messages from Link clients
 * 
 * Protocol: 4-byte big-endian length prefix, then serialized protobuf bytes.
 * All sockets are non-blocking with 10ms poll interval.
 */
class AuroreLinkServer {
public:
    explicit AuroreLinkServer(const AuroreLinkConfig& cfg = {});
    ~AuroreLinkServer();

    bool start();
    void stop();

    // Broadcast to all connected telemetry clients (thread-safe)
    void broadcast_telemetry(const Telemetry& msg);
    // Broadcast annotated video frame (thread-safe)
    void broadcast_video(const VideoFrame& frame);

    void set_mode_callback(ModeCallback cb);
    void set_freecam_callback(FreecamCallback cb);

    size_t client_count() const;
    LinkMode current_mode() const { return mode_.load(std::memory_order_acquire); }

private:
    void telemetry_accept_loop();
    void video_accept_loop();
    void command_accept_loop();
    bool send_length_prefixed(int fd, const std::string& serialized);

    AuroreLinkConfig cfg_;
    std::atomic<bool> running_{false};
    std::atomic<LinkMode> mode_{LinkMode::AUTO};

    int telemetry_fd_{-1};
    int video_fd_{-1};
    int command_fd_{-1};

    std::thread telemetry_accept_thread_;
    std::thread video_accept_thread_;
    std::thread command_accept_thread_;

    mutable std::mutex clients_mutex_;
    std::vector<int> telemetry_clients_;
    std::vector<int> video_clients_;
    std::vector<int> command_clients_;

    ModeCallback    on_mode_;
    FreecamCallback on_freecam_;
};

}  // namespace aurore
