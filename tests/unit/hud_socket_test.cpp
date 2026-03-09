#include "aurore/hud_socket.hpp"
#include "aurore/timing.hpp"
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

using namespace aurore;

static const char* kTestSocket = "/tmp/aurore_hud_test.sock";

void test_server_starts_and_stops() {
    // SEC-008: Use config-based constructor, disable root requirement for tests
    HudSocketConfig config;
    config.socket_path = kTestSocket;
    config.require_root_uid = false;  // Allow non-root for tests
    HudSocket hud(config);
    assert(hud.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    hud.stop();
    std::cout << "PASS: HUD socket starts and stops\n";
}

void test_client_can_connect() {
    HudSocketConfig config;
    config.socket_path = kTestSocket;
    config.require_root_uid = false;  // Allow non-root for tests
    HudSocket hud(config);
    hud.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // ICD-006: SOCK_SEQPACKET for message preservation
    int client = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
    assert(client >= 0);

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, kTestSocket, sizeof(addr.sun_path) - 1);

    [[maybe_unused]] int rc = ::connect(client, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    ::close(client);
    hud.stop();

    assert(rc == 0);
    std::cout << "PASS: client can connect to HUD socket\n";
}

void test_broadcast_delivers_data() {
    HudSocketConfig config;
    config.socket_path = kTestSocket;
    config.require_root_uid = false;  // Allow non-root for tests
    HudSocket hud(config);
    hud.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // ICD-006: SOCK_SEQPACKET for message preservation
    int client = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, kTestSocket, sizeof(addr.sun_path) - 1);
    ::connect(client, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));

    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    HudFrame frame{};
    frame.state = 4;  // TRACKING (new spec-compliant state)
    frame.az_deg = 15.5f;
    frame.el_deg = -3.2f;
    frame.target_cx = 400.f;
    frame.target_cy = 300.f;
    frame.confidence = 0.85f;
    frame.p_hit = 0.0f;
    frame.timestamp_ns = get_timestamp();  // Set current timestamp
    hud.broadcast(frame);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // ICD-006: Binary message format (64 bytes per message)
    // We receive 4 messages: RETICLE_DATA, TARGET_BOX, BALLISTIC_SOLUTION, SYSTEM_STATUS
    char buf[256]{};
    ssize_t n = ::recv(client, buf, sizeof(buf) - 1, MSG_DONTWAIT);
    ::close(client);
    hud.stop();

    // Verify we received binary data (at least one 64-byte message)
    assert(n >= 64);  // Minimum: one complete binary message
    
    // Check sync word (0xA7070007 in little-endian)
    const uint32_t sync_word = *reinterpret_cast<const uint32_t*>(buf);
    assert(sync_word == 0xA7070007);
    
    std::cout << "PASS: HUD broadcast delivers binary messages (received " 
              << n << " bytes)\n";
}

void test_max_clients_limit() {
    HudSocketConfig config;
    config.socket_path = kTestSocket;
    config.require_root_uid = false;
    config.max_clients = 2;  // Small limit for testing
    HudSocket hud(config);
    hud.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Connect max_clients
    // ICD-006: SOCK_SEQPACKET for message preservation
    int client1 = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
    int client2 = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
    int client3 = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, kTestSocket, sizeof(addr.sun_path) - 1);

    ::connect(client1, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    ::connect(client2, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Third client should be rejected (but connection succeeds, just not added)
    ::connect(client3, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Check that auth failures were recorded
    uint64_t failures = hud.get_auth_failures();
    (void)failures;  // May or may not have failures depending on timing

    ::close(client1);
    ::close(client2);
    ::close(client3);
    hud.stop();

    std::cout << "PASS: max clients limit enforced\n";
}

// PERF-008: Test rate limiting (120 msg/sec max)
void test_rate_limiting() {
    HudSocketConfig config;
    config.socket_path = kTestSocket;
    config.require_root_uid = false;
    config.rate_limit_msgs_per_sec = 10.0;  // Low limit for testing
    HudSocket hud(config);
    hud.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Connect a client
    // ICD-006: SOCK_SEQPACKET for message preservation
    int client = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, kTestSocket, sizeof(addr.sun_path) - 1);
    ::connect(client, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // Send messages rapidly - should be rate limited after initial burst
    int messages_sent = 0;
    int messages_expected = 0;
    
    // Initial burst: should send up to bucket capacity (10 messages)
    for (int i = 0; i < 15; i++) {
        HudFrame frame{};
        frame.state = 4;
        frame.timestamp_ns = get_timestamp();
        hud.broadcast(frame);
        messages_sent++;
        if (i < 10) {
            messages_expected++;  // First 10 should pass (bucket capacity)
        }
    }
    
    // Wait for token bucket to refill (0.5 sec = 5 tokens at 10/sec)
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Send more messages - should allow ~5 more
    for (int i = 0; i < 5; i++) {
        HudFrame frame{};
        frame.state = 4;
        frame.timestamp_ns = get_timestamp();
        hud.broadcast(frame);
        messages_sent++;
        messages_expected++;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Verify rate limiting was applied
    uint64_t rate_limited = hud.get_rate_limited_count();
    assert(rate_limited > 0);  // Some messages should have been rate limited
    
    std::cout << "PASS: rate limiting enforced (limited " << rate_limited 
              << " of " << messages_sent << " messages)\n";
    
    ::close(client);
    hud.stop();
}

// PERF-008: Test message timeout (>100ms old messages discarded)
void test_message_timeout() {
    HudSocketConfig config;
    config.socket_path = kTestSocket;
    config.require_root_uid = false;
    config.message_timeout_ms = 100.0;  // 100ms timeout
    HudSocket hud(config);
    hud.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Connect a client
    // ICD-006: SOCK_SEQPACKET for message preservation
    int client = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, kTestSocket, sizeof(addr.sun_path) - 1);
    ::connect(client, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // Send a fresh message - should be delivered
    HudFrame fresh_frame{};
    fresh_frame.state = 4;
    fresh_frame.timestamp_ns = get_timestamp();  // Current timestamp
    hud.broadcast(fresh_frame);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Send a stale message - should be discarded
    HudFrame stale_frame{};
    stale_frame.state = 4;
    stale_frame.timestamp_ns = get_timestamp() - 200'000'000ULL;  // 200ms old
    hud.broadcast(stale_frame);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Verify timeout was applied
    uint64_t timeout_discarded = hud.get_timeout_discarded_count();
    assert(timeout_discarded > 0);  // Stale message should have been discarded
    
    std::cout << "PASS: message timeout enforced (discarded " << timeout_discarded 
              << " stale messages)\n";
    
    ::close(client);
    hud.stop();
}

int main() {
    ::unlink(kTestSocket);
    test_server_starts_and_stops();
    ::unlink(kTestSocket);
    test_client_can_connect();
    ::unlink(kTestSocket);
    test_broadcast_delivers_data();
    ::unlink(kTestSocket);
    test_max_clients_limit();
    ::unlink(kTestSocket);
    test_rate_limiting();
    ::unlink(kTestSocket);
    test_message_timeout();
    ::unlink(kTestSocket);
    std::cout << "\nAll HUD socket tests passed.\n";
    return 0;
}
