/**
 * @file test_emergency_inhibit.cpp
 * @brief Unit tests for EMERGENCY_INHIBIT message handler
 *
 * Tests verify:
 * - EMERGENCY_INHIBIT (0x0109) triggers emergency stop callback
 * - No authentication required for emergency stop
 * - Immediate action without HMAC verification
 */

#include "aurore/aurore_link_server.hpp"
#include "aurore.pb.h"
#include <arpa/inet.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <google/protobuf/stubs/common.h>

using namespace aurore;

// Connect a raw TCP client to the given port
static int connect_to(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd); return -1;
    }
    return fd;
}

void test_emergency_stop_callback_fires() {
    std::cout << "Running test_emergency_stop_callback_fires...\n";
    
    AuroreLinkConfig cfg;
    cfg.telemetry_port = 19160;
    cfg.video_port     = 19161;
    cfg.command_port   = 19162;
    cfg.hmac_key = "test_key";  // Enable HMAC for binary command testing
    AuroreLinkServer server(cfg);

    std::atomic<bool> emergency_fired{false};
    server.set_emergency_stop_callback([&]() {
        emergency_fired.store(true, std::memory_order_release);
        std::cout << "  Emergency stop callback fired\n";
    });
    
    assert(server.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    int cmd_client = connect_to(19162);
    assert(cmd_client >= 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Send EMERGENCY_INHIBIT message (0x0109)
    // Per spec: No authentication required for emergency stop
    aurore::LinkInputMessage emergency_msg{};
    emergency_msg.header.sync_word = 0xA7050005;  // AURORE05
    emergency_msg.header.message_id = static_cast<uint16_t>(aurore::LinkMsgId::kEmergencyInhibit);
    emergency_msg.header.sequence = 0;
    emergency_msg.header.timestamp_ns = 0;
    // No HMAC required for EMERGENCY_INHIBIT per spec

    [[maybe_unused]] ssize_t sent = ::send(cmd_client, &emergency_msg, sizeof(emergency_msg), 0);
    assert(sent == sizeof(emergency_msg));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    assert(emergency_fired.load(std::memory_order_acquire));

    ::close(cmd_client);
    server.stop();
    std::cout << "  PASS: emergency stop callback fires on EMERGENCY_INHIBIT message\n";
}

void test_emergency_stop_no_auth_required() {
    std::cout << "Running test_emergency_stop_no_auth_required...\n";
    
    AuroreLinkConfig cfg;
    cfg.telemetry_port = 19170;
    cfg.video_port     = 19171;
    cfg.command_port   = 19172;
    cfg.hmac_key = "test_key_with_auth_enabled";  // HMAC enabled
    AuroreLinkServer server(cfg);

    std::atomic<bool> emergency_fired{false};
    server.set_emergency_stop_callback([&]() {
        emergency_fired.store(true, std::memory_order_release);
    });
    
    assert(server.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    int cmd_client = connect_to(19172);
    assert(cmd_client >= 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Send EMERGENCY_INHIBIT with invalid/empty HMAC
    // Should still be processed (no auth required per spec)
    aurore::LinkInputMessage emergency_msg{};
    emergency_msg.header.sync_word = 0xA7050005;  // AURORE05
    emergency_msg.header.message_id = static_cast<uint16_t>(aurore::LinkMsgId::kEmergencyInhibit);
    emergency_msg.header.sequence = 0;
    emergency_msg.header.timestamp_ns = 0;
    // HMAC is zero-filled (invalid) - should still work for emergency stop

    [[maybe_unused]] ssize_t sent = ::send(cmd_client, &emergency_msg, sizeof(emergency_msg), 0);
    assert(sent == sizeof(emergency_msg));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Emergency stop should fire despite invalid HMAC
    assert(emergency_fired.load(std::memory_order_acquire));

    ::close(cmd_client);
    server.stop();
    std::cout << "  PASS: emergency stop works without authentication\n";
}

void test_emergency_stop_message_id() {
    std::cout << "Running test_emergency_stop_message_id...\n";
    
    // Verify the message ID is correct per ICD-005
    assert(static_cast<uint16_t>(aurore::LinkMsgId::kEmergencyInhibit) == 0x0109);
    std::cout << "  PASS: EMERGENCY_INHIBIT message ID is 0x0109\n";
}

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    
    std::cout << "=====================================\n";
    std::cout << "EMERGENCY_INHIBIT Handler Tests\n";
    std::cout << "=====================================\n\n";
    
    test_emergency_stop_message_id();
    test_emergency_stop_callback_fires();
    test_emergency_stop_no_auth_required();
    
    std::cout << "\n=====================================\n";
    std::cout << "All EMERGENCY_INHIBIT tests passed.\n";
    std::cout << "=====================================\n";
    
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
