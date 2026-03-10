#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#if defined(__aarch64__) || defined(__arm__)
#include <arm_neon.h>
#define HAS_NEON
#endif

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
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " != " #b); } while(0)

// Scalar implementation
void convert_scalar(const uint8_t* raw, uint8_t* bgr, int width, int height, int stride) {
    for (int row = 0; row < height; ++row) {
        const uint8_t* line = raw + row * stride;
        uint8_t* out = bgr + row * width * 3;
        for (int col = 0; col < width; col += 4) {
            const uint16_t p0 = (static_cast<uint16_t>(line[0]) << 2) | (line[4] & 0x03u);
            const uint16_t p1 = (static_cast<uint16_t>(line[1]) << 2) | ((line[4] >> 2) & 0x03u);
            const uint16_t p2 = (static_cast<uint16_t>(line[2]) << 2) | ((line[4] >> 4) & 0x03u);
            const uint16_t p3 = (static_cast<uint16_t>(line[3]) << 2) | ((line[4] >> 6) & 0x03u);
            line += 5;

            const auto to_u8 = [](uint16_t v) -> uint8_t {
                return static_cast<uint8_t>(v >> 2);
            };
            
            for (int i = 0; i < 4; ++i) {
                if (col + i < width) {
                    uint16_t p = (i==0?p0:(i==1?p1:(i==2?p2:p3)));
                    uint8_t val = to_u8(p);
                    out[(col+i)*3 + 0] = val;
                    out[(col+i)*3 + 1] = val;
                    out[(col+i)*3 + 2] = val;
                }
            }
        }
    }
}

#ifdef HAS_NEON
void convert_neon(const uint8_t* raw, uint8_t* bgr, int width, int height, int stride) {
    for (int row = 0; row < height; ++row) {
        const uint8_t* line = raw + row * stride;
        uint8_t* out = bgr + row * width * 3;

        int col = 0;
        for (; col <= width - 32; col += 32) {
            uint8x8x5_t v = vld5_u8(line);
            line += 40;

            for (int i = 0; i < 4; ++i) {
                uint8x8x3_t bgr_v;
                bgr_v.val[0] = v.val[i];
                bgr_v.val[1] = v.val[i];
                bgr_v.val[2] = v.val[i];
                vst3_u8(out, bgr_v);
                out += 24;
            }
        }
        // Software fallback for remainder
        for (; col < width; col += 4) {
            const uint16_t p0 = (static_cast<uint16_t>(line[0]) << 2) | (line[4] & 0x03u);
            const uint16_t p1 = (static_cast<uint16_t>(line[1]) << 2) | ((line[4] >> 2) & 0x03u);
            const uint16_t p2 = (static_cast<uint16_t>(line[2]) << 2) | ((line[4] >> 4) & 0x03u);
            const uint16_t p3 = (static_cast<uint16_t>(line[3]) << 2) | ((line[4] >> 6) & 0x03u);
            line += 5;
            const auto to_u8 = [](uint16_t v) -> uint8_t { return static_cast<uint8_t>(v >> 2); };
            if (col+0 < width) { uint8_t v=to_u8(p0); out[0]=v; out[1]=v; out[2]=v; out+=3; }
            if (col+1 < width) { uint8_t v=to_u8(p1); out[0]=v; out[1]=v; out[2]=v; out+=3; }
            if (col+2 < width) { uint8_t v=to_u8(p2); out[0]=v; out[1]=v; out[2]=v; out+=3; }
            if (col+3 < width) { uint8_t v=to_u8(p3); out[0]=v; out[1]=v; out[2]=v; out+=3; }
        }
    }
}
#endif

}  // anonymous namespace

// 59. Raw10 Conversion: Logic verification
TEST(test_raw10_conversion_logic) {
    const int width = 64;
    const int height = 4;
    const int stride = (width * 10 + 7) / 8; // 80 bytes
    
    std::vector<uint8_t> raw(stride * height);
    for (size_t i = 0; i < raw.size(); ++i) raw[i] = static_cast<uint8_t>(i % 256);
    
    std::vector<uint8_t> bgr_scalar(width * height * 3, 0);
    convert_scalar(raw.data(), bgr_scalar.data(), width, height, stride);
    
#ifdef HAS_NEON
    std::vector<uint8_t> bgr_neon(width * height * 3, 0);
    convert_neon(raw.data(), bgr_neon.data(), width, height, stride);
    
    for (size_t i = 0; i < bgr_scalar.size(); ++i) {
        if (bgr_scalar[i] != bgr_neon[i]) {
            std::cerr << "Mismatch at index " << i << ": scalar=" << (int)bgr_scalar[i] 
                      << ", neon=" << (int)bgr_neon[i] << std::endl;
            throw std::runtime_error("NEON logic mismatch");
        }
    }
    std::cout << "  NEON logic matches scalar" << std::endl;
#else
    std::cout << "  NEON not available, skipping comparison" << std::endl;
#endif

    // Basic value check (first pixel)
    // p0 = (line[0] << 2) | (line[4] & 0x03)
    // line[0]=0, line[4]=4 -> p0 = 4. to_u8(4) = 1.
    // Wait, line[0] is high 8 bits? Yes. (line[0]<<2) | (low bits).
    // line[0]=0, line[4]=4 -> p0 = 4. val = 4 >> 2 = 1.
    ASSERT_EQ(bgr_scalar[0], 0); // byte 0 is high 8 bits, so val = byte 0.
    // Let's check the code: to_u8(p0) = (p0 >> 2).
    // p0 = (line[0] << 2) | ...
    // to_u8(p0) = ((line[0] << 2) | ...) >> 2 = line[0].
    ASSERT_EQ(bgr_scalar[0], raw[0]);
}

int main() {
    std::cout << "Running Raw10 Conversion tests..." << std::endl;
    RUN_TEST(test_raw10_conversion_logic);
    
    std::cout << "Tests run: " << g_tests_run.load() << std::endl;
    std::cout << "Tests passed: " << g_tests_passed.load() << std::endl;
    return g_tests_failed.load() > 0 ? 1 : 0;
}
