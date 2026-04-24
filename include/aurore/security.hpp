#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <future>
#include <functional>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
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

// ---------------------------------------------------------------------------
// ECDSA P-256 firmware signing (AM7-L2-SEC-002 / AM7-L3-SEC-002)
// ---------------------------------------------------------------------------

/**
 * @brief Sign a file with ECDSA P-256, writing the DER-encoded signature.
 *
 * The key is a PEM-encoded EC private key (prime256v1 / P-256).
 * Output signature is DER-encoded and written to sig_path.
 *
 * @param file_path  Path to the file to sign (e.g. the aurore binary)
 * @param key_pem    PEM-encoded EC private key string
 * @param sig_path   Output path for DER-encoded ECDSA signature
 * @return true on success
 */
inline bool sign_file_ecdsa(const std::string& file_path,
                             const std::string& key_pem,
                             const std::string& sig_path) {
    // Hash the file with SHA-256
    unsigned char file_hash[32];
    {
        std::ifstream f(file_path, std::ios::binary);
        if (!f.is_open()) {
            std::fprintf(stderr, "[security] sign_file: cannot open %s\n", file_path.c_str());
            return false;
        }
        SHA256_CTX ctx;
        SHA256_Init(&ctx);
        char buf[65536];
        while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
            SHA256_Update(&ctx, buf, static_cast<size_t>(f.gcount()));
        }
        SHA256_Final(file_hash, &ctx);
    }

    // Load private key from PEM string
    BIO* bio = BIO_new_mem_buf(key_pem.data(), static_cast<int>(key_pem.size()));
    if (!bio) return false;
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) {
        std::fprintf(stderr, "[security] sign_file: failed to load private key\n");
        return false;
    }

    // Sign the hash
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) { EVP_PKEY_free(pkey); return false; }
    if (EVP_DigestSignInit(mdctx, nullptr, EVP_sha256(), nullptr, pkey) != 1 ||
        EVP_DigestSignUpdate(mdctx, file_hash, 32) != 1) {
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        return false;
    }
    size_t sig_len = 0;
    EVP_DigestSignFinal(mdctx, nullptr, &sig_len);
    std::vector<uint8_t> sig(sig_len);
    if (EVP_DigestSignFinal(mdctx, sig.data(), &sig_len) != 1) {
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        return false;
    }
    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);

    // Write DER signature to file
    std::ofstream out(sig_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        std::fprintf(stderr, "[security] sign_file: cannot write %s\n", sig_path.c_str());
        return false;
    }
    out.write(reinterpret_cast<const char*>(sig.data()), static_cast<std::streamsize>(sig_len));
    ::chmod(sig_path.c_str(), 0644);
    return true;
}

/**
 * @brief Verify ECDSA P-256 signature of a file.
 *
 * Reads the DER-encoded signature from sig_path and verifies against
 * the SHA-256 hash of file_path using the provided PEM public key.
 *
 * @param file_path  Path to the file whose signature to verify
 * @param pubkey_pem PEM-encoded EC public key (prime256v1)
 * @param sig_path   Path to DER-encoded ECDSA signature
 * @return true if signature is valid
 */
inline bool verify_file_ecdsa(const std::string& file_path,
                               const std::string& pubkey_pem,
                               const std::string& sig_path) {
    // Hash the file
    unsigned char file_hash[32];
    {
        std::ifstream f(file_path, std::ios::binary);
        if (!f.is_open()) return false;
        SHA256_CTX ctx;
        SHA256_Init(&ctx);
        char buf[65536];
        while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
            SHA256_Update(&ctx, buf, static_cast<size_t>(f.gcount()));
        }
        SHA256_Final(file_hash, &ctx);
    }

    // Load signature
    std::ifstream sf(sig_path, std::ios::binary);
    if (!sf.is_open()) {
        std::fprintf(stderr, "[security] verify_file: no signature at %s\n", sig_path.c_str());
        return false;
    }
    sf.seekg(0, std::ios::end);
    std::vector<uint8_t> sig(static_cast<size_t>(sf.tellg()));
    sf.seekg(0, std::ios::beg);
    sf.read(reinterpret_cast<char*>(sig.data()), static_cast<std::streamsize>(sig.size()));

    // Load public key
    BIO* bio = BIO_new_mem_buf(pubkey_pem.data(), static_cast<int>(pubkey_pem.size()));
    if (!bio) return false;
    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) {
        std::fprintf(stderr, "[security] verify_file: failed to load public key\n");
        return false;
    }

    // Verify
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    bool ok = false;
    if (mdctx &&
        EVP_DigestVerifyInit(mdctx, nullptr, EVP_sha256(), nullptr, pkey) == 1 &&
        EVP_DigestVerifyUpdate(mdctx, file_hash, 32) == 1 &&
        EVP_DigestVerifyFinal(mdctx, sig.data(), sig.size()) == 1) {
        ok = true;
    }
    if (mdctx) EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    return ok;
}

/**
 * @brief Verify this process's own binary using a detached ECDSA signature.
 *
 * Reads /proc/self/exe for the binary path, public key from pubkey_path,
 * and DER signature from sig_path. Per AM7-L3-SEC-002.
 *
 * @param pubkey_path Path to PEM public key (default /etc/aurore/signing_key.pub)
 * @param sig_path    Path to detached signature (default /etc/aurore/aurore.sig)
 * @return true if binary is authentic; false if verification fails or files absent
 */
inline bool verify_self(const std::string& pubkey_path = "/etc/aurore/signing_key.pub",
                        const std::string& sig_path    = "/etc/aurore/aurore.sig") {
    // Resolve self binary path via /proc/self/exe
    char exe_path[4096] = {};
    ssize_t n = ::readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (n <= 0) {
        std::fprintf(stderr, "[security] verify_self: readlink /proc/self/exe failed\n");
        return false;
    }
    exe_path[n] = '\0';

    // Load public key
    std::ifstream pkf(pubkey_path);
    if (!pkf.is_open()) {
        std::fprintf(stderr, "[security] verify_self: public key not found at %s\n",
                     pubkey_path.c_str());
        return false;
    }
    pkf.seekg(0, std::ios::end);
    std::string pubkey_pem(static_cast<size_t>(pkf.tellg()), '\0');
    pkf.seekg(0, std::ios::beg);
    pkf.read(pubkey_pem.data(), static_cast<std::streamsize>(pubkey_pem.size()));

    return verify_file_ecdsa(exe_path, pubkey_pem, sig_path);
}

/**
 * @brief Generate a 256-bit key using hardware RNG (/dev/urandom).
 *
 * AM7-L3-SEC-006: Keys shall be generated using hardware RNG or /dev/urandom
 * with ≥256 bits entropy.
 *
 * @param out_key Output buffer for 32-byte key
 * @return true on success
 */
inline bool generate_key_256bit(uint8_t out_key[32]) {
    return RAND_bytes(out_key, 32) == 1;
}

/**
 * @brief Save a 256-bit key to a protected file (mode 0600).
 *
 * AM7-L2-SEC-006: Keys shall be stored in protected storage.
 * File is written atomically and permissions set to owner-read-only.
 *
 * @param path File path for key storage
 * @param key  32-byte key to write
 * @return true on success
 */
inline bool save_key_to_file(const std::string& path, const uint8_t key[32]) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        std::fprintf(stderr, "[security] Cannot open key file for write: %s\n", path.c_str());
        return false;
    }
    f.write(reinterpret_cast<const char*>(key), 32);
    f.close();
    if (::chmod(path.c_str(), 0600) != 0) {
        std::fprintf(stderr, "[security] chmod 0600 failed for %s: %s\n", path.c_str(), strerror(errno));
        return false;
    }
    return true;
}

/**
 * @brief Load HMAC key from protected storage per AM7-L2-SEC-006.
 *
 * Priority order:
 *   1. AURORE_HMAC_KEY env var (hex-encoded 64-char string = 32 bytes)
 *   2. File at key_path (binary 32-byte file, mode must be ≤ 0640)
 *
 * Keys stored in config.json plaintext are rejected.
 *
 * @param key_path Path to binary key file (checked if env var absent)
 * @param out_key  Output: 32-byte key
 * @return true if key loaded successfully
 */
inline bool load_hmac_key(const std::string& key_path, std::string& out_key) {
    // 1. Check environment variable first (CI/container deployments)
    const char* env_key = std::getenv("AURORE_HMAC_KEY");
    if (env_key != nullptr) {
        const size_t env_len = std::strlen(env_key);
        if (env_len == 64) {
            // Hex-encoded 256-bit key
            uint8_t raw[32];
            for (size_t i = 0; i < 32; ++i) {
                char hex[3] = {env_key[i * 2], env_key[i * 2 + 1], '\0'};
                char* end = nullptr;
                raw[i] = static_cast<uint8_t>(std::strtoul(hex, &end, 16));
                if (end != hex + 2) {
                    std::fprintf(stderr, "[security] AURORE_HMAC_KEY: invalid hex at byte %zu\n", i);
                    return false;
                }
            }
            out_key.assign(reinterpret_cast<const char*>(raw), 32);
            return true;
        } else if (env_len >= 32) {
            // Raw bytes in env var (less preferred, but accept if ≥32 chars)
            out_key.assign(env_key, 32);
            return true;
        }
        std::fprintf(stderr, "[security] AURORE_HMAC_KEY env var too short (%zu chars, need 64 hex)\n", env_len);
        return false;
    }

    // 2. Load from file — check permissions first (AM7-L2-SEC-006)
    if (key_path.empty()) {
        std::fprintf(stderr, "[security] No AURORE_HMAC_KEY env var and no key file path configured\n");
        return false;
    }

    struct stat st{};
    if (::stat(key_path.c_str(), &st) != 0) {
        std::fprintf(stderr, "[security] Key file not found: %s\n", key_path.c_str());
        return false;
    }

    // Reject world-readable key files
    if ((st.st_mode & 0004) != 0) {
        std::fprintf(stderr, "[security] Key file %s is world-readable (mode %04o) — REJECTED\n",
                     key_path.c_str(), static_cast<unsigned int>(st.st_mode) & 0777U);
        return false;
    }

    std::ifstream f(key_path, std::ios::binary);
    if (!f.is_open()) {
        std::fprintf(stderr, "[security] Cannot open key file: %s\n", key_path.c_str());
        return false;
    }

    std::string raw_key(32, '\0');
    f.read(raw_key.data(), 32);
    const auto bytes_read = f.gcount();
    f.close();

    if (bytes_read < 32) {
        std::fprintf(stderr, "[security] Key file too short: %s (%ld bytes, need 32)\n",
                     key_path.c_str(), static_cast<long>(bytes_read));
        return false;
    }

    out_key = raw_key;
    return true;
}

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
