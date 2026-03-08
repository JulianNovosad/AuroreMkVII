#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>

namespace aurore {
namespace security {

/**
 * @brief Computes HMAC-SHA256 signature for a given message using a secret key.
 * 
 * @param key The secret key for signing.
 * @param message The message to sign.
 * @return std::string The hexadecimal representation of the HMAC signature.
 */
inline std::string compute_hmac_sha256(const std::string& key, const std::string& message) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int len = 0;

    HMAC(EVP_sha256(), 
         key.c_str(), static_cast<int>(key.length()),
         reinterpret_cast<const unsigned char*>(message.c_str()), message.length(),
         hash, &len);

    std::stringstream ss;
    for (unsigned int i = 0; i < len; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

/**
 * @brief Verifies an HMAC-SHA256 signature.
 * 
 * @param key The secret key.
 * @param message The message to verify.
 * @param signature The received signature to verify against.
 * @return true if the signature is valid, false otherwise.
 */
inline bool verify_hmac_sha256(const std::string& key, const std::string& message, const std::string& signature) {
    std::string computed = compute_hmac_sha256(key, message);
    // Constant-time comparison to prevent timing attacks
    if (computed.length() != signature.length()) {
        return false;
    }

    unsigned char result = 0;
    for (size_t i = 0; i < computed.length(); i++) {
        result |= static_cast<unsigned char>(static_cast<unsigned char>(computed[i]) ^ static_cast<unsigned char>(signature[i]));
    }
    return result == 0;
}

} // namespace security
} // namespace aurore
