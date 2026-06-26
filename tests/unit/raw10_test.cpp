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
#define ASSERT_EQ(a, b)                                                              \
    do {                                                                             \
        if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " != " #b); \
    } while (0)

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

            const auto to_u8 = [](uint16_t v) -> uint8_t { return static_cast<uint8_t>(v >> 2); };

            for (int i = 0; i < 4; ++i) {
                if (col + i < width) {
                    uint16_t p = (i == 0 ? p0 : (i == 1 ? p1 : (i == 2 ? p2 : p3)));
                    uint8_t val = to_u8(p);
                    out[(col + i) * 3 + 0] = val;
                    out[(col + i) * 3 + 1] = val;
                    out[(col + i) * 3 + 2] = val;
                }
            }
        }
    }
}

#ifdef HAS_NEON
// Matches the production implementation in camera_wrapper.cpp (vtbl1_u8 path).
// RAW10: 4 pixels packed in 5 bytes [p0h][p1h][p2h][p3h][ctrl].
// We only use the high 8 bits (p0h…p3h); low 2 bits are discarded (same
// result as >> 2 of the full 10-bit value since p_n_h == (10bit >> 2)).
void convert_neon(const uint8_t* raw, uint8_t* bgr, int width, int height, int stride) {
    // Permutation: pull 8 pixel high-bytes out of 10 input bytes
    // Input layout (10 bytes = 2 groups): [p0,p1,p2,p3,C0,p4,p5,p6,p7,C1]
    // vld1_u8 loads first 8: [p0,p1,p2,p3,C0,p4,p5,p6]
    // kPerm maps: [0,1,2,3,5,6,7,0] → [p0,p1,p2,p3,p4,p5,p6,p0]
    // then vset_lane_u8(p7, px, 7) fixes lane 7 → [p0,p1,p2,p3,p4,p5,p6,p7]
    static const uint8_t kPerm[8] = {0, 1, 2, 3, 5, 6, 7, 0};
    const uint8x8_t perm = vld1_u8(kPerm);

    for (int row = 0; row < height; ++row) {
        const uint8_t* line = raw + row * stride;
        uint8_t* out = bgr + row * width * 3;
        int col = 0;

        for (; col <= width - 8; col += 8) {
            uint8x8_t g = vld1_u8(line);
            const uint8_t p7 = line[8];
            line += 10;

            uint8x8_t px = vtbl1_u8(g, perm);
            px = vset_lane_u8(p7, px, 7);

            uint8x8x3_t bgr_v;
            bgr_v.val[0] = px;
            bgr_v.val[1] = px;
            bgr_v.val[2] = px;
            vst3_u8(out, bgr_v);
            out += 24;
        }

        for (; col < width; col += 4) {
            const uint8_t g0 = line[0];
            const uint8_t g1 = line[1];
            const uint8_t g2 = line[2];
            const uint8_t g3 = line[3];
            line += 5;
            if (col + 0 < width) {
                out[0] = g0;
                out[1] = g0;
                out[2] = g0;
                out += 3;
            }
            if (col + 1 < width) {
                out[0] = g1;
                out[1] = g1;
                out[2] = g1;
                out += 3;
            }
            if (col + 2 < width) {
                out[0] = g2;
                out[1] = g2;
                out[2] = g2;
                out += 3;
            }
            if (col + 3 < width) {
                out[0] = g3;
                out[1] = g3;
                out[2] = g3;
                out += 3;
            }
        }
    }
}
#endif

}  // anonymous namespace

// 59. Raw10 Conversion: Logic verification
TEST(test_raw10_conversion_logic) {
    const int width = 64;
    const int height = 4;
    const int stride = (width * 10 + 7) / 8;  // 80 bytes

    std::vector<uint8_t> raw(stride * height);
    for (size_t i = 0; i < raw.size(); ++i) raw[i] = static_cast<uint8_t>(i % 256);

    std::vector<uint8_t> bgr_scalar(width * height * 3, 0);
    convert_scalar(raw.data(), bgr_scalar.data(), width, height, stride);

#ifdef HAS_NEON
    std::vector<uint8_t> bgr_neon(width * height * 3, 0);
    convert_neon(raw.data(), bgr_neon.data(), width, height, stride);

    for (size_t i = 0; i < bgr_scalar.size(); ++i) {
        if (bgr_scalar[i] != bgr_neon[i]) {
            std::cerr << "Mismatch at index " << i << ": scalar=" << static_cast<int>(bgr_scalar[i])
                      << ", neon=" << static_cast<int>(bgr_neon[i]) << std::endl;
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
    ASSERT_EQ(bgr_scalar[0], 0);  // byte 0 is high 8 bits, so val = byte 0.
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
