#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <filesystem>

#include "aurore/hud_socket.hpp"
#include "aurore/security.hpp"
#include "aurore/timing.hpp"

namespace fs = std::filesystem;

namespace {

// Test counters
std::atomic<size_t> g_tests_run(0);
std::atomic<size_t> g_tests_passed(0);
std::atomic<size_t> g_tests_failed(0);

#define TEST(name) void name()
#define RUN_TEST(name) do { \
    g_tests_run.fetch_add(1); \
    try { \
        name(); \
        g_tests_passed.fetch_add(1); \
        std::cout << "  PASS: " << #name << std::endl; \
    } \
    catch (const std::exception& e) { \
        g_tests_failed.fetch_add(1); \
        std::cerr << "  FAIL: " << #name << " - " << e.what() << std::endl; \
    } \
} while(0)

#define ASSERT_TRUE(x) do { if (!(x)) throw std::runtime_error("Assertion failed: " #x); } while(0)
#define ASSERT_FALSE(x) ASSERT_TRUE(!(x))
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " != " #b); } while(0)
#define ASSERT_GT(a, b) do { if (!((a) > (b))) throw std::runtime_error("Assertion failed: " #a " <= " #b); } while(0)

}  // anonymous namespace

using namespace aurore;

TEST(test_hud_socket_scenarios) {
    HudSocketConfig config;
    config.socket_path = "/tmp/aurore_test/hud_telemetry.sock";
    config.hmac_key = "secret_key_12345678901234567890";
    config.require_root_uid = false; 
    config.allowed_uid = getuid();
    config.rate_limit_msgs_per_sec = 10.0;
    
    fs::create_directories("/tmp/aurore_test");
    unlink(config.socket_path.c_str());
    
    HudSocket server(config);
    ASSERT_TRUE(server.start());
    
    int client_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, config.socket_path.c_str(), sizeof(addr.sun_path)-1);
    
    int ret = connect(client_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (ret != 0) {
        throw std::runtime_error("Connect failed: " + std::string(strerror(errno)));
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_EQ(server.get_client_count(), 1);
    
    HudFrame frame;
    frame.timestamp_ns = aurore::get_timestamp();
    server.broadcast(frame);

    // ICD-006: broadcast() sends exactly 4 messages per call — drain all 4
    const std::array<uint16_t, 4> kExpectedMsgIds = {
        static_cast<uint16_t>(HudMsgId::kReticleData),        // 0x0301
        static_cast<uint16_t>(HudMsgId::kTargetBox),          // 0x0302
        static_cast<uint16_t>(HudMsgId::kBallisticSolution),  // 0x0303
        static_cast<uint16_t>(HudMsgId::kSystemStatus),       // 0x0304
    };

    std::array<HudBinaryMessage, 4> msgs{};
    for (size_t i = 0; i < 4; ++i) {
        ssize_t n = read(client_fd, &msgs[i], sizeof(HudBinaryMessage));
        ASSERT_EQ(n, static_cast<ssize_t>(sizeof(HudBinaryMessage)));
    }

    // Verify HMAC on each message
    for (size_t i = 0; i < 4; ++i) {
        ASSERT_TRUE(security::verify_hmac_sha256_raw(
            config.hmac_key, &msgs[i], sizeof(HudBinaryHeader) + 32, msgs[i].hmac));
    }

    // ICD-006: all 4 messages share the same sequence number per broadcast
    const uint32_t seq0 = msgs[0].header.sequence;
    for (size_t i = 1; i < 4; ++i) {
        ASSERT_EQ(msgs[i].header.sequence, seq0);
    }

    // Verify the 4 expected message IDs are present in order
    for (size_t i = 0; i < 4; ++i) {
        ASSERT_EQ(msgs[i].header.message_id, kExpectedMsgIds[i]);
    }

    // Verify HMAC tamper detection on the first message
    msgs[0].hmac[0] ^= 0xFF;
    ASSERT_FALSE(security::verify_hmac_sha256_raw(
        config.hmac_key, &msgs[0], sizeof(HudBinaryHeader) + 32, msgs[0].hmac));
    
    uint64_t initial_limited = server.get_rate_limited_count();
    for (int i = 0; i < 50; ++i) {
        frame.timestamp_ns = aurore::get_timestamp();
        server.broadcast(frame);
    }
    
    ASSERT_GT(server.get_rate_limited_count(), initial_limited);
    
    close(client_fd);
    server.stop();
}

int main() {
    std::cout << "Running HudSocket Stress tests..." << std::endl;
    RUN_TEST(test_hud_socket_scenarios);
    
    std::cout << "Tests run: " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed: " << g_tests_passed.load() << std::endl;
    return g_tests_failed.load() > 0 ? 1 : 0;
}
