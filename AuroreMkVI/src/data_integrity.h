#ifndef DATA_INTEGRITY_H
#define DATA_INTEGRITY_H

#include <cstdint>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <array>
#include <functional>

namespace aurore {
namespace integrity {

constexpr uint32_t CRC32_POLYNOMIAL = 0xEDB88320;
constexpr size_t SHA256_DIGEST_SIZE = 32;

inline uint32_t calculate_crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        uint8_t byte = data[i];
        crc ^= byte;
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFF;
}

inline uint32_t calculate_crc32(const std::vector<uint8_t>& data) {
    return calculate_crc32(data.data(), data.size());
}

class SHA256 {
public:
    static std::array<uint8_t, SHA256_DIGEST_SIZE> hash(const uint8_t* data, size_t length) {
        std::array<uint8_t, SHA256_DIGEST_SIZE> digest{};
        
        uint32_t h[8] = {
            0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
            0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19
        };
        
        uint64_t bit_length = length * 8;
        size_t padded_len = ((length + 9 + 63) / 64) * 64;
        std::vector<uint8_t> padded(padded_len, 0);
        
        std::memcpy(padded.data(), data, length);
        padded[length] = 0x80;
        
        padded[padded_len - 8] = static_cast<uint8_t>((bit_length >> 56) & 0xFF);
        padded[padded_len - 7] = static_cast<uint8_t>((bit_length >> 48) & 0xFF);
        padded[padded_len - 6] = static_cast<uint8_t>((bit_length >> 40) & 0xFF);
        padded[padded_len - 5] = static_cast<uint8_t>((bit_length >> 32) & 0xFF);
        padded[padded_len - 4] = static_cast<uint8_t>((bit_length >> 24) & 0xFF);
        padded[padded_len - 3] = static_cast<uint8_t>((bit_length >> 16) & 0xFF);
        padded[padded_len - 2] = static_cast<uint8_t>((bit_length >> 8) & 0xFF);
        padded[padded_len - 1] = static_cast<uint8_t>(bit_length & 0xFF);
        
        for (size_t i = 0; i < padded_len; i += 64) {
            uint32_t w[64];
            for (int j = 0; j < 16; ++j) {
                w[j] = (padded[i + j * 4] << 24) |
                       (padded[i + j * 4 + 1] << 16) |
                       (padded[i + j * 4 + 2] << 8) |
                       (padded[i + j * 4 + 3]);
            }
            for (int j = 16; j < 64; ++j) {
                uint32_t s0 = ROTR(w[j-15], 7) ^ ROTR(w[j-15], 18) ^ (w[j-15] >> 3);
                uint32_t s1 = ROTR(w[j-2], 17) ^ ROTR(w[j-2], 19) ^ (w[j-2] >> 10);
                w[j] = w[j-16] + s0 + w[j-7] + s1;
            }
            
            uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
            uint32_t e = h[4], f = h[5], g = h[6], h_val = h[7];
            
            for (int j = 0; j < 64; ++j) {
                uint32_t S1 = ROTR(e, 6) ^ ROTR(e, 11) ^ ROTR(e, 25);
                uint32_t ch = (e & f) ^ ((~e) & g);
                uint32_t temp1 = h_val + S1 + ch + K[j] + w[j];
                uint32_t S0 = ROTR(a, 2) ^ ROTR(a, 13) ^ ROTR(a, 22);
                uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                uint32_t temp2 = S0 + maj;
                
                h_val = g;
                g = f;
                f = e;
                e = d + temp1;
                d = c;
                c = b;
                b = a;
                a = temp1 + temp2;
            }
            
            h[0] += a; h[1] += b; h[2] += c; h[3] += d;
            h[4] += e; h[5] += f; h[6] += g; h[7] += h_val;
        }
        
        for (int i = 0; i < 8; ++i) {
            digest[i * 4] = static_cast<uint8_t>((h[i] >> 24) & 0xFF);
            digest[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xFF);
            digest[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xFF);
            digest[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xFF);
        }
        
        return digest;
    }
    
    static std::array<uint8_t, SHA256_DIGEST_SIZE> hash(const std::vector<uint8_t>& data) {
        return hash(data.data(), data.size());
    }
    
    static std::string hash_to_string(const std::array<uint8_t, SHA256_DIGEST_SIZE>& digest) {
        std::stringstream ss;
        for (uint8_t byte : digest) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        }
        return ss.str();
    }
    
    static std::array<uint8_t, SHA256_DIGEST_SIZE> hash_file(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) return {};
        
        std::vector<uint8_t> buffer(8192);
        std::vector<uint8_t> all_data;
        
        while (file.read(reinterpret_cast<char*>(buffer.data()), buffer.size())) {
            all_data.insert(all_data.end(), buffer.begin(), buffer.begin() + file.gcount());
        }
        if (file.gcount() > 0) {
            all_data.insert(all_data.end(), buffer.begin(), buffer.begin() + file.gcount());
        }
        
        return hash(all_data);
    }
    
    static std::string hash_file_to_string(const std::string& filename) {
        auto digest = hash_file(filename);
        return hash_to_string(digest);
    }
    
    static bool verify_file(const std::string& filename, const std::string& expected_hash) {
        auto actual = hash_file_to_string(filename);
        return actual == expected_hash;
    }

private:
    static uint32_t ROTR(uint32_t x, int n) {
        return (x >> n) | (x << (32 - n));
    }
    
    static constexpr uint32_t K[64] = {
        0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
        0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3, 0x72BE5D74, 0x80DEB1FE, 0x9BDDC06A, 0xC19BF174,
        0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
        0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
        0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13, 0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
        0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
        0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
        0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208, 0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2
    };
};

class DataIntegrityChecker {
public:
    struct IntegrityCheck {
        bool passed;
        uint32_t crc32;
        std::string sha256;
        std::string message;
    };

    static IntegrityCheck verify_frame(const uint8_t* data, size_t length, uint32_t expected_crc) {
        IntegrityCheck result;
        result.crc32 = calculate_crc32(data, length);
        result.passed = (result.crc32 == expected_crc);
        result.message = result.passed ? "Frame integrity OK" : "Frame corruption detected";
        return result;
    }
    
    static IntegrityCheck verify_file(const std::string& filepath, const std::string& expected_sha256) {
        IntegrityCheck result;
        auto digest = SHA256::hash_file(filepath);
        result.sha256 = SHA256::hash_to_string(digest);
        result.passed = (result.sha256 == expected_sha256);
        result.message = result.passed ? "File integrity OK" : "File corruption detected";
        result.crc32 = 0;
        return result;
    }
    
    static IntegrityCheck verify_config_file(const std::string& filepath, const std::string& expected_sha256) {
        auto result = verify_file(filepath, expected_sha256);
        if (!result.passed) {
            result.message = "CONFIG FILE CORRUPTED: " + result.message;
        }
        return result;
    }
    
    static IntegrityCheck verify_model_file(const std::string& filepath, const std::string& expected_sha256) {
        auto result = verify_file(filepath, expected_sha256);
        if (!result.passed) {
            result.message = "MODEL FILE CORRUPTED: " + result.message;
        }
        return result;
    }
};

inline DataIntegrityChecker::IntegrityCheck check_frame_integrity(const uint8_t* data, size_t length, uint32_t expected_crc) {
    return DataIntegrityChecker::verify_frame(data, length, expected_crc);
}

inline DataIntegrityChecker::IntegrityCheck check_file_integrity(const std::string& filepath, const std::string& expected_sha256) {
    return DataIntegrityChecker::verify_file(filepath, expected_sha256);
}

}  // namespace integrity
}  // namespace aurore

#endif  // DATA_INTEGRITY_H
