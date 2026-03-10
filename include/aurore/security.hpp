#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <thread>
#include <functional>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <iomanip>
#include <sstream>

// Suppress deprecation warnings for SHA256_* functions (they're faster than EVP API)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace aurore {
namespace security {

/**
 * @brief Computes SHA256 hash of a raw binary buffer.
 *
 * Uses legacy SHA256_* API for performance (EVP API is 10x slower).
 *
 * @param data Pointer to the data buffer.
 * @param len Length of the data buffer.
 * @param out_hash Pointer to a buffer where the 32-byte hash will be stored.
 */
inline void compute_sha256_raw(const void* data, size_t len, unsigned char* out_hash) {
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, data, len);
    SHA256_Final(out_hash, &ctx);
}

/**
 * @brief Computes SHA256 hash of a raw binary buffer (thread-safe version).
 *
 * Uses legacy SHA256_* API for performance (EVP API is 10x slower).
 * Each call creates a new context for thread safety.
 *
 * @param data Pointer to the data buffer.
 * @param len Length of the data buffer.
 * @param out_hash Pointer to a buffer where the 32-byte hash will be stored.
 */
inline void compute_sha256_raw_threadsafe(const void* data, size_t len, unsigned char* out_hash) {
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, data, len);
    SHA256_Final(out_hash, &ctx);
}

/**
 * @brief Computes HMAC-SHA256 signature for a raw binary buffer.
 *
 * @param key The secret key for signing.
 * @param data Pointer to the data buffer.
 * @param len Length of the data buffer.
 * @param out_hmac Pointer to a buffer where the 32-byte HMAC will be stored.
 */
inline void compute_hmac_sha256_raw(const std::string& key, const void* data, size_t len, unsigned char* out_hmac) {
    unsigned int hmac_len = 0;
    HMAC(EVP_sha256(),
         key.c_str(), static_cast<int>(key.length()),
         reinterpret_cast<const unsigned char*>(data), len,
         out_hmac, &hmac_len);
}

/**
 * @brief Computes HMAC-SHA256 signature for a raw binary buffer (thread-safe version).
 *
 * Uses EVP_PKEY_new_raw_private_key for proper key handling.
 * Requires two-phase EVP_DigestSignFinal call to get buffer size first.
 *
 * @param key The secret key for signing.
 * @param data Pointer to the data buffer.
 * @param len Length of the data buffer.
 * @param out_hmac Pointer to a buffer where the 32-byte HMAC will be stored.
 */
inline void compute_hmac_sha256_raw_threadsafe(const std::string& key, const void* data, size_t len, unsigned char* out_hmac) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        std::fprintf(stderr, "FATAL: EVP_MD_CTX_new failed (out of memory or OpenSSL error)\n");
        std::abort();
    }

    // Create key from raw bytes using EVP_PKEY_new_raw_private_key (correct API for HMAC)
    EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_HMAC, nullptr,
        reinterpret_cast<const unsigned char*>(key.data()), key.size()
    );
    if (!pkey) {
        std::fprintf(stderr, "FATAL: EVP_PKEY_new_raw_private_key failed (invalid key or OpenSSL error)\n");
        EVP_MD_CTX_free(ctx);
        std::abort();
    }

    if (EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) != 1) {
        std::fprintf(stderr, "FATAL: EVP_DigestSignInit failed\n");
        EVP_PKEY_free(pkey);
        EVP_MD_CTX_free(ctx);
        std::abort();
    }
    if (EVP_DigestSignUpdate(ctx, data, len) != 1) {
        std::fprintf(stderr, "FATAL: EVP_DigestSignUpdate failed\n");
        EVP_PKEY_free(pkey);
        EVP_MD_CTX_free(ctx);
        std::abort();
    }

    // Two-phase final: first get required buffer size, then compute HMAC
    size_t hmac_len = 0;
    if (EVP_DigestSignFinal(ctx, nullptr, &hmac_len) != 1) {
        std::fprintf(stderr, "FATAL: EVP_DigestSignFinal (get size) failed\n");
        EVP_PKEY_free(pkey);
        EVP_MD_CTX_free(ctx);
        std::abort();
    }
    if (EVP_DigestSignFinal(ctx, out_hmac, &hmac_len) != 1) {
        std::fprintf(stderr, "FATAL: EVP_DigestSignFinal (write) failed\n");
        EVP_PKEY_free(pkey);
        EVP_MD_CTX_free(ctx);
        std::abort();
    }

    EVP_PKEY_free(pkey);
    EVP_MD_CTX_free(ctx);
}

/**
 * @brief Verifies a raw HMAC-SHA256 signature using constant-time comparison.
 *
 * @param key The secret key.
 * @param data Pointer to the data.
 * @param len Length of the data.
 * @param signature Pointer to the 32-byte signature to verify.
 * @return true if valid, false otherwise.
 */
inline bool verify_hmac_sha256_raw(const std::string& key, const void* data, size_t len, const unsigned char* signature) {
    unsigned char computed[32];
    compute_hmac_sha256_raw(key, data, len, computed);

    unsigned char diff = 0;
    for (int i = 0; i < 32; i++) {
        diff |= static_cast<unsigned char>(computed[i] ^ signature[i]);
    }
    return diff == 0;
}

/**
 * @brief RFC 1982 sequence number comparison (wrap-aware).
 *
 * Implements RFC 1982 "Serial Number Arithmetic" for 32-bit sequence numbers.
 * Handles wrap-around correctly for sequence numbers in the range [0, 2^32-1].
 *
 * @param current The received sequence number
 * @param expected The expected next sequence number
 * @return true if current is valid (>= expected, accounting for wrap)
 */
bool verify_sequence_number(uint32_t current, uint32_t expected);

/**
 * @brief Detect sequence gaps with configurable threshold.
 *
 * Checks if the gap between old and new sequence numbers exceeds threshold.
 * Handles wrap-around correctly using RFC 1982 arithmetic.
 *
 * @param old_seq The previous sequence number
 * @param new_seq The new sequence number
 * @param threshold Maximum allowed gap before triggering alert
 * @return true if gap > threshold (security concern)
 */
bool is_sequence_gap(uint32_t old_seq, uint32_t new_seq, uint32_t threshold);

/**
 * @brief Async frame authentication helper.
 * 
 * Computes SHA256 hash and HMAC-SHA256 asynchronously to avoid blocking
 * the critical path. Uses a background thread for hash computation.
 * 
 * Usage:
 * @code
 *     AsyncFrameAuthenticator auth(hmac_key);
 *     
 *     // After frame capture, submit for async authentication
 *     auth.authenticate_frame(pixel_data, pixel_size, frame_header, frame);
 *     
 *     // Wait for completion (optional, with timeout)
 *     if (auth.wait_for_completion(std::chrono::milliseconds(8))) {
 *         // Frame is authenticated
 *     }
 * @endcode
 */
class AsyncFrameAuthenticator {
public:
    /**
     * @brief Construct authenticator with HMAC key.
     * 
     * @param hmac_key 256-bit HMAC key (32 bytes recommended)
     */
    explicit AsyncFrameAuthenticator(const std::string& hmac_key)
        : hmac_key_(hmac_key)
        , busy_(false)
        , completed_(false)
        , success_(false) {}

    /**
     * @brief Authenticate frame asynchronously.
     * 
     * Computes SHA256 of pixel data, then HMAC-SHA256 over header + hash.
     * Results are written to the ZeroCopyFrame when complete.
     * 
     * @param pixel_data Pointer to pixel data buffer
     * @param pixel_size Size of pixel data in bytes
     * @param header_data Pointer to frame header data (for HMAC)
     * @param header_size Size of header data in bytes
     * @param out_frame Pointer to frame struct to write hash/hmac
     */
    void authenticate_frame(const void* pixel_data, size_t pixel_size,
                           const void* header_data, size_t header_size,
                           void* out_frame);

    /**
     * @brief Wait for authentication to complete.
     * 
     * @param timeout Maximum time to wait
     * @return true if authentication completed, false on timeout
     */
    bool wait_for_completion(std::chrono::milliseconds timeout);

    /**
     * @brief Check if authentication is in progress.
     * 
     * @return true if authentication is running
     */
    bool is_busy() const noexcept {
        return busy_.load(std::memory_order_acquire);
    }

    /**
     * @brief Check if last authentication succeeded.
     * 
     * @return true if authentication completed successfully
     */
    bool last_success() const noexcept {
        return success_.load(std::memory_order_acquire);
    }

private:
    std::string hmac_key_;
    std::atomic<bool> busy_;
    std::atomic<bool> completed_;
    std::atomic<bool> success_;
    std::thread worker_;
    
    // Working buffers
    unsigned char pending_hash_[32];
    unsigned char pending_hmac_[32];
    void* pending_frame_ = nullptr;
};

// Inline implementations for AsyncFrameAuthenticator
inline void AsyncFrameAuthenticator::authenticate_frame(
    const void* pixel_data, size_t pixel_size,
    const void* header_data, size_t header_size,
    void* out_frame) {

    if (busy_.exchange(true, std::memory_order_acq_rel)) {
        // Already busy - contract violation (frame dropped)
        std::fprintf(stderr, "FATAL: AsyncFrameAuthenticator is busy. Frame dropped! (backpressure violation)\n");
        std::abort();
    }

    completed_.store(false, std::memory_order_release);
    success_.store(false, std::memory_order_release);
    pending_frame_ = out_frame;

    // Copy input data for async processing
    std::memcpy(pending_hash_, pixel_data, std::min(pixel_size, static_cast<size_t>(32)));

    worker_ = std::thread([this, pixel_data, pixel_size, header_data, header_size]() {
        // Compute SHA256 of pixel data
        unsigned char frame_hash[32];
        compute_sha256_raw_threadsafe(pixel_data, pixel_size, frame_hash);

        // Compute HMAC over header + hash
        std::vector<uint8_t> hmac_input;
        hmac_input.reserve(header_size + 32);
        hmac_input.insert(hmac_input.end(),
                         static_cast<const uint8_t*>(header_data),
                         static_cast<const uint8_t*>(header_data) + header_size);
        hmac_input.insert(hmac_input.end(), frame_hash, frame_hash + 32);

        unsigned char hmac[32];
        compute_hmac_sha256_raw_threadsafe(hmac_key_, hmac_input.data(), hmac_input.size(), hmac);

        // Write results to ZeroCopyFrame struct
        // Note: ZeroCopyFrame has frame_hash[32] and hmac[32] as consecutive fields
        // Offsets: frame_hash at 269, hmac at 301 (on 64-bit platform)
        struct ZeroCopyFrameAuth {
            // Fields before frame_hash (must match ZeroCopyFrame layout exactly)
            uint64_t sequence;         // 0
            uint64_t timestamp_ns;     // 8
            uint64_t exposure_us;      // 16
            float gain;                // 24
            void* plane_data[4];       // 32
            size_t plane_size[4];      // 64
            int stride[4];             // 96
            int width;                 // 112
            int height;                // 116
            uint32_t format;           // 120 (PixelFormat underlying type)
            void* request_ptr;         // 128
            uint32_t buffer_id;        // 136
            bool valid;                // 140
            char error[128];           // 141
            // Authentication fields
            uint8_t frame_hash[32];    // 269
            uint8_t hmac[32];          // 301
        };

        ZeroCopyFrameAuth* auth_frame = static_cast<ZeroCopyFrameAuth*>(pending_frame_);
        if (auth_frame) {
            std::memcpy(auth_frame->frame_hash, frame_hash, 32);
            std::memcpy(auth_frame->hmac, hmac, 32);
        }

        std::memcpy(pending_hash_, frame_hash, 32);
        std::memcpy(pending_hmac_, hmac, 32);

        success_.store(true, std::memory_order_release);
        completed_.store(true, std::memory_order_release);
        busy_.store(false, std::memory_order_release);
    });

    worker_.detach();  // Fire-and-forget for async operation
}

inline bool AsyncFrameAuthenticator::wait_for_completion(std::chrono::milliseconds timeout) {
    auto start = std::chrono::steady_clock::now();
    while (!completed_.load(std::memory_order_acquire)) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed >= timeout) {
            return false;
        }
        std::this_thread::yield();
    }
    return success_.load(std::memory_order_acquire);
}

} // namespace security
} // namespace aurore

#pragma GCC diagnostic pop
