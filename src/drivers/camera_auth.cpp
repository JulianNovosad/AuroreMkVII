/**
 * @file camera_auth.cpp
 * @brief Frame authentication logic for ZeroCopyFrame
 *
 * Implements SHA256 hash computation and HMAC-SHA256 authentication
 * for camera frames per ICD-001 and AM7-L2-SEC-001.
 */

#include "aurore/camera_auth.hpp"
#include "aurore/camera_wrapper.hpp"
#include "aurore/security.hpp"

#include <cstring>
#include <vector>
#include <string>

namespace aurore {

// Default HMAC key for development (should be loaded from config in production)
// This is a 256-bit key stored in .rodata
const char* kDefaultHmacKey = "AURORE_MK7_FRAME_AUTH_KEY_256BIT_SECRET";

/**
 * @brief Compute frame header for HMAC input.
 * 
 * Packs the frame header fields into a contiguous buffer for HMAC computation.
 * Per ICD-001: HMAC covers header + frame_hash.
 */
void compute_frame_header(const ZeroCopyFrame& frame, uint8_t* out_header, size_t& out_size) {
    // Header layout (matches ICD-001 spec):
    // - sequence:      u64 (8 bytes)
    // - timestamp_ns:  u64 (8 bytes)
    // - exposure_us:   u64 (8 bytes)
    // - gain:          f32 (4 bytes)
    // - width:         u32 (4 bytes)
    // - height:        u32 (4 bytes)
    // - format:        u32 (4 bytes)
    // - buffer_id:     u32 (4 bytes)
    // Total: 44 bytes
    
    size_t offset = 0;
    std::memcpy(out_header + offset, &frame.sequence, sizeof(frame.sequence));
    offset += sizeof(frame.sequence);
    std::memcpy(out_header + offset, &frame.timestamp_ns, sizeof(frame.timestamp_ns));
    offset += sizeof(frame.timestamp_ns);
    std::memcpy(out_header + offset, &frame.exposure_us, sizeof(frame.exposure_us));
    offset += sizeof(frame.exposure_us);
    std::memcpy(out_header + offset, &frame.gain, sizeof(frame.gain));
    offset += sizeof(frame.gain);
    std::memcpy(out_header + offset, &frame.width, sizeof(frame.width));
    offset += sizeof(frame.width);
    std::memcpy(out_header + offset, &frame.height, sizeof(frame.height));
    offset += sizeof(frame.height);
    std::memcpy(out_header + offset, &frame.format, sizeof(frame.format));
    offset += sizeof(frame.format);
    std::memcpy(out_header + offset, &frame.buffer_id, sizeof(frame.buffer_id));
    offset += sizeof(frame.buffer_id);
    
    out_size = offset;
}

/**
 * @brief Compute SHA256 hash of frame pixel data.
 * 
 * @param frame Frame to hash
 * @return true if hash computed successfully
 */
bool compute_frame_hash(ZeroCopyFrame& frame) {
    if (!frame.is_valid() || frame.plane_data[0] == nullptr || frame.plane_size[0] == 0) {
        return false;
    }
    
    // Compute SHA256 of pixel data (plane 0 only for RAW10)
    aurore::security::compute_sha256_raw_threadsafe(
        frame.plane_data[0], 
        frame.plane_size[0], 
        frame.frame_hash
    );
    
    return true;
}

/**
 * @brief Compute HMAC-SHA256 over frame header + hash.
 * 
 * @param frame Frame to authenticate (must have frame_hash computed)
 * @param hmac_key HMAC key (256-bit recommended)
 * @param key_len Length of key in bytes
 * @return true if HMAC computed successfully
 */
bool compute_frame_hmac(ZeroCopyFrame& frame, const void* hmac_key, size_t key_len) {
    if (!frame.is_valid()) {
        return false;
    }
    
    // Build header buffer
    uint8_t header_buf[64];  // 44 bytes needed
    size_t header_size = 0;
    compute_frame_header(frame, header_buf, header_size);
    
    // Compute HMAC over header + frame_hash
    // Input: header (44 bytes) + frame_hash (32 bytes) = 76 bytes
    std::vector<uint8_t> hmac_input;
    hmac_input.reserve(header_size + 32);
    hmac_input.insert(hmac_input.end(), header_buf, header_buf + header_size);
    hmac_input.insert(hmac_input.end(), frame.frame_hash, frame.frame_hash + 32);
    
    std::string key_str(static_cast<const char*>(hmac_key), key_len);
    aurore::security::compute_hmac_sha256_raw_threadsafe(
        key_str,
        hmac_input.data(),
        hmac_input.size(),
        frame.hmac
    );
    
    return true;
}

/**
 * @brief Authenticate frame (compute hash + HMAC).
 * 
 * This is the main entry point for frame authentication.
 * Called after frame capture, before releasing to consumer.
 * 
 * @param frame Frame to authenticate
 * @param hmac_key HMAC key (nullptr uses default key)
 * @param key_len Length of key (0 uses default key length)
 * @return true if authentication successful
 */
bool authenticate_frame(ZeroCopyFrame& frame, const void* hmac_key, size_t key_len) {
    // Compute SHA256 hash of pixel data
    if (!compute_frame_hash(frame)) {
        return false;
    }
    
    // Use default key if none provided
    if (!hmac_key || key_len == 0) {
        hmac_key = kDefaultHmacKey;
        key_len = std::strlen(kDefaultHmacKey);
    }
    
    // Compute HMAC over header + hash
    return compute_frame_hmac(frame, hmac_key, key_len);
}

/**
 * @brief Verify frame authentication.
 *
 * Member function implementation for ZeroCopyFrame::verify_authentication.
 * Recomputes the frame hash from pixel data and verifies the HMAC.
 *
 * @param key HMAC key
 * @param key_len Length of key
 * @return true if verification passes
 */
bool ZeroCopyFrame::verify_authentication(const void* key, size_t key_len) const noexcept {
    if (!is_valid()) {
        return false;
    }

    // Recompute SHA256 hash of pixel data to detect tampering
    unsigned char computed_hash[32];
    if (plane_data[0] == nullptr || plane_size[0] == 0) {
        return false;
    }
    aurore::security::compute_sha256_raw_threadsafe(
        plane_data[0],
        plane_size[0],
        computed_hash
    );

    // Check if hash matches (detects pixel data tampering)
    if (std::memcmp(computed_hash, frame_hash, 32) != 0) {
        return false;
    }

    // Rebuild header
    uint8_t header_buf[64];
    size_t header_size = 0;
    compute_frame_header(*this, header_buf, header_size);

    // Rebuild HMAC input (header + frame_hash)
    std::vector<uint8_t> hmac_input;
    hmac_input.reserve(header_size + 32);
    hmac_input.insert(hmac_input.end(), header_buf, header_buf + header_size);
    hmac_input.insert(hmac_input.end(), frame_hash, frame_hash + 32);

    // Verify HMAC
    std::string key_str(static_cast<const char*>(key), key_len);
    return aurore::security::verify_hmac_sha256_raw(key_str, hmac_input.data(), hmac_input.size(), hmac);
}

}  // namespace aurore
