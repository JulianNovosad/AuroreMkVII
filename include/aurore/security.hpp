#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <future>
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
inline bool compute_hmac_sha256_raw_threadsafe(const std::string& key, const void* data, size_t len, unsigned char* out_hmac) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        std::fprintf(stderr, "[security] EVP_MD_CTX_new failed\n");
        return false;
    }

    EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_HMAC, nullptr,
        reinterpret_cast<const unsigned char*>(key.data()), key.size()
    );
    if (!pkey) {
        std::fprintf(stderr, "[security] EVP_PKEY_new_raw_private_key failed\n");
        EVP_MD_CTX_free(ctx);
        return false;
    }

    if (EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) != 1 ||
        EVP_DigestSignUpdate(ctx, data, len) != 1) {
        std::fprintf(stderr, "[security] EVP_DigestSign init/update failed\n");
        EVP_PKEY_free(pkey);
        EVP_MD_CTX_free(ctx);
        return false;
    }

    size_t hmac_len = 0;
    if (EVP_DigestSignFinal(ctx, nullptr, &hmac_len) != 1 ||
        EVP_DigestSignFinal(ctx, out_hmac, &hmac_len) != 1) {
        std::fprintf(stderr, "[security] EVP_DigestSignFinal failed\n");
        EVP_PKEY_free(pkey);
        EVP_MD_CTX_free(ctx);
        return false;
    }

    EVP_PKEY_free(pkey);
    EVP_MD_CTX_free(ctx);
    return true;
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

    // Constant-time comparison: prevents timing side-channel attacks
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
        : hmac_key_(hmac_key) {}

    ~AsyncFrameAuthenticator() {
        if (worker_future_.valid()) worker_future_.wait();
    }

    AsyncFrameAuthenticator(const AsyncFrameAuthenticator&) = delete;
    AsyncFrameAuthenticator& operator=(const AsyncFrameAuthenticator&) = delete;

    /**
     * @brief Authenticate frame asynchronously.
     *
     * Computes SHA256 of pixel data, then HMAC-SHA256 over header + hash.
     * Results are written to out_hash (32 bytes) and out_hmac (32 bytes).
     *
     * @param pixel_data Pointer to pixel data buffer
     * @param pixel_size Size of pixel data in bytes
     * @param header_data Pointer to frame header data (for HMAC)
     * @param header_size Size of header data in bytes
     * @param out_hash Output buffer for SHA256 hash (32 bytes, e.g. frame.frame_hash)
     * @param out_hmac Output buffer for HMAC-SHA256 (32 bytes, e.g. frame.hmac)
     */
    void authenticate_frame(const void* pixel_data, size_t pixel_size,
                           const void* header_data, size_t header_size,
                           uint8_t* out_hash, uint8_t* out_hmac);

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
        if (!worker_future_.valid()) return false;
        return worker_future_.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout;
    }

    /**
     * @brief Check if last authentication succeeded.
     * 
     * @return true if authentication completed successfully
     */
    bool last_success() const noexcept {
        return result_;
    }

private:
    std::string hmac_key_;
    mutable std::future<bool> worker_future_;
    bool result_ = false;

    uint8_t* pending_out_hash_ = nullptr;
    uint8_t* pending_out_hmac_ = nullptr;
};

// Inline implementations for AsyncFrameAuthenticator
inline void AsyncFrameAuthenticator::authenticate_frame(
    const void* pixel_data, size_t pixel_size,
    const void* header_data, size_t header_size,
    uint8_t* out_hash, uint8_t* out_hmac) {

    if (worker_future_.valid() &&
        worker_future_.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout) {
        std::fprintf(stderr, "[security] AsyncFrameAuthenticator busy — frame dropped (backpressure violation)\n");
        result_ = false;
        return;
    }

    pending_out_hash_ = out_hash;
    pending_out_hmac_ = out_hmac;

    worker_future_ = std::async(std::launch::async,
        [this, pixel_data, pixel_size, header_data, header_size]() -> bool {
            unsigned char frame_hash[32];
            compute_sha256_raw_threadsafe(pixel_data, pixel_size, frame_hash);

            std::vector<uint8_t> hmac_input;
            hmac_input.reserve(header_size + 32);
            hmac_input.insert(hmac_input.end(),
                             static_cast<const uint8_t*>(header_data),
                             static_cast<const uint8_t*>(header_data) + header_size);
            hmac_input.insert(hmac_input.end(), frame_hash, frame_hash + 32);

            unsigned char hmac[32];
            if (!compute_hmac_sha256_raw_threadsafe(hmac_key_, hmac_input.data(), hmac_input.size(), hmac)) {
                return false;
            }

            if (pending_out_hash_) std::memcpy(pending_out_hash_, frame_hash, 32);
            if (pending_out_hmac_) std::memcpy(pending_out_hmac_, hmac, 32);

            return true;
        });
}

inline bool AsyncFrameAuthenticator::wait_for_completion(std::chrono::milliseconds timeout) {
    if (!worker_future_.valid()) return result_;
    auto status = worker_future_.wait_for(timeout);
    if (status == std::future_status::ready) {
        result_ = worker_future_.get();  // invalidates future
        return result_;
    }
    return false;  // timed out; destructor will block for cleanup
}

} // namespace security
} // namespace aurore

#pragma GCC diagnostic pop
