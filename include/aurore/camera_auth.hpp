/**
 * @file camera_auth.hpp
 * @brief Frame authentication logic for ZeroCopyFrame
 *
 * Implements SHA256 hash computation and HMAC-SHA256 authentication
 * for camera frames per ICD-001 and AM7-L2-SEC-001.
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace aurore {

struct ZeroCopyFrame;

/**
 * @brief Default HMAC key for development (should be loaded from config in production)
 */
extern const char* kDefaultHmacKey;

/**
 * @brief Compute frame header for HMAC input.
 * 
 * Packs the frame header fields into a contiguous buffer for HMAC computation.
 * Per ICD-001: HMAC covers header + frame_hash.
 */
void compute_frame_header(const ZeroCopyFrame& frame, uint8_t* out_header, size_t& out_size);

/**
 * @brief Compute SHA256 hash of frame pixel data.
 * 
 * @param frame Frame to hash (hash written to frame.frame_hash)
 * @return true if hash computed successfully
 */
bool compute_frame_hash(ZeroCopyFrame& frame);

/**
 * @brief Compute HMAC-SHA256 over frame header + hash.
 * 
 * @param frame Frame to authenticate (must have frame_hash computed)
 * @param hmac_key HMAC key (256-bit recommended)
 * @param key_len Length of key in bytes
 * @return true if HMAC computed successfully
 */
bool compute_frame_hmac(ZeroCopyFrame& frame, const void* hmac_key, size_t key_len);

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
bool authenticate_frame(ZeroCopyFrame& frame, const void* hmac_key = nullptr, size_t key_len = 0);

}  // namespace aurore
