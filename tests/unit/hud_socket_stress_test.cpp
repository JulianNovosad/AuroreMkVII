#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

#include "aurore/hud_socket.hpp"
#include "aurore/timing.hpp"

namespace fs = std::filesystem;

namespace {

std::atomic<size_t> g_tests_run(0);
std::atomic<size_t> g_tests_passed(0);
std::atomic<size_t> g_tests_failed(0);

#define RUN_TEST(name)                                                          \
    do {                                                                        \
        g_tests_run.fetch_add(1);                                               \
        try {                                                                   \
            name();                                                             \
            g_tests_passed.fetch_add(1);                                        \
            std::cout << "  PASS: " << #name << std::endl;                      \
        } catch (const std::exception& e) {                                     \
            g_tests_failed.fetch_add(1);                                        \
            std::cerr << "  FAIL: " << #name << " - " << e.what() << std::endl; \
        }                                                                       \
    } while (0)

#define ASSERT_TRUE(x)                                               \
    do {                                                             \
        if (!(x)) throw std::runtime_error("Assertion failed: " #x); \
    } while (0)
#define ASSERT_GT(a, b)                                                                \
    do {                                                                               \
        if (!((a) > (b))) throw std::runtime_error("Assertion failed: " #a " <= " #b); \
    } while (0)
#define ASSERT_EQ(a, b)                                                              \
    do {                                                                             \
        if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " != " #b); \
    } while (0)

static int connect_unix(const char* path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

}  // namespace

using namespace aurore;

static const char* kSockPath = "/tmp/aurore_stress_test.sock";

// Verify broadcast delivers JSON to a single client
void test_json_broadcast_delivers_data() {
    HudSocketConfig cfg;
    cfg.socket_path = kSockPath;
    cfg.require_root_uid = false;
    ::unlink(kSockPath);

    HudSocket server(cfg);
    ASSERT_TRUE(server.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    int fd = connect_unix(kSockPath);
    if (fd < 0) throw std::runtime_error("Connect failed: " + std::string(strerror(errno)));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ASSERT_EQ(server.get_client_count(), 1);

    HudFrame frame{};
    frame.timestamp_ns = get_timestamp();
    frame.state = 4;
    frame.az_deg = 12.5f;
    server.broadcast(frame);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    char buf[512]{};
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
    ASSERT_GT(n, 0);
    ASSERT_TRUE(buf[0] == '{');  // JSON object

    close(fd);
    server.stop();
}

// Rate limiting: 50 bursted messages with 10/sec limit — some must be rate-limited
void test_rate_limiting_under_load() {
    HudSocketConfig cfg;
    cfg.socket_path = kSockPath;
    cfg.require_root_uid = false;
    cfg.rate_limit_msgs_per_sec = 10.0;
    ::unlink(kSockPath);

    HudSocket server(cfg);
    ASSERT_TRUE(server.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    int fd = connect_unix(kSockPath);
    if (fd < 0) throw std::runtime_error("Connect failed");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    uint64_t before = server.get_rate_limited_count();
    for (int i = 0; i < 50; i++) {
        HudFrame f{};
        f.timestamp_ns = get_timestamp();
        server.broadcast(f);
    }
    ASSERT_GT(server.get_rate_limited_count(), before);

    close(fd);
    server.stop();
}

// Stale messages older than message_timeout_ms must be discarded
void test_stale_message_discarded() {
    HudSocketConfig cfg;
    cfg.socket_path = kSockPath;
    cfg.require_root_uid = false;
    cfg.message_timeout_ms = 100.0;
    ::unlink(kSockPath);

    HudSocket server(cfg);
    ASSERT_TRUE(server.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    int fd = connect_unix(kSockPath);
    if (fd < 0) throw std::runtime_error("Connect failed");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    HudFrame stale{};
    stale.timestamp_ns = get_timestamp() - 200'000'000ULL;  // 200ms old
    server.broadcast(stale);

    uint64_t discarded = server.get_timeout_discarded_count();
    ASSERT_GT(discarded, 0);

    close(fd);
    server.stop();
}

int main() {
    std::cout << "Running HudSocket Stress tests..." << std::endl;
    RUN_TEST(test_json_broadcast_delivers_data);
    RUN_TEST(test_rate_limiting_under_load);
    RUN_TEST(test_stale_message_discarded);

    std::cout << "Tests run: " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed: " << g_tests_passed.load() << std::endl;
    return g_tests_failed.load() > 0 ? 1 : 0;
}
