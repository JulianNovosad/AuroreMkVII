#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace aurore {

/**
 * @brief UNIX domain socket command bridge (Node.js → C++ state machine).
 *
 * Listens on a UNIX socket path and reads newline-delimited text commands
 * from connected clients (e.g., the aurore-link Node.js server).
 *
 * Protocol (newline-terminated lines):
 *   MODE AUTO        → on_mode("AUTO")
 *   MODE FREECAM     → on_mode("FREECAM")
 *   MODE IDLE        → on_mode("IDLE")
 *   FREECAM AZ EL    → on_freecam(az_deg, el_deg)
 *   RESET            → on_reset()
 */
class CommandSocket {
public:
    using ModeCallback    = std::function<void(const std::string& mode)>;
    using FreecamCallback = std::function<void(float az_deg, float el_deg)>;
    using ResetCallback   = std::function<void()>;

    struct Config {
        std::string socket_path = "/tmp/aurore_cmd.sock";
    };

    CommandSocket();
    explicit CommandSocket(const Config& cfg);
    ~CommandSocket();

    bool start();
    void stop();

    void set_mode_callback(ModeCallback cb);
    void set_freecam_callback(FreecamCallback cb);
    void set_reset_callback(ResetCallback cb);

private:
    void accept_loop();
    void client_loop(int fd);
    void dispatch(const std::string& line);

    Config               cfg_;
    int                  server_fd_{-1};
    std::atomic<bool>    running_{false};
    std::thread          accept_thread_;

    mutable std::mutex   clients_mutex_;
    std::vector<int>     client_fds_;

    mutable std::mutex   cb_mutex_;
    ModeCallback         on_mode_;
    FreecamCallback      on_freecam_;
    ResetCallback        on_reset_;
};

}  // namespace aurore
