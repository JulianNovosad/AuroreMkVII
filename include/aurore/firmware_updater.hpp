#pragma once
// AM7-L3-SEC-005: Firmware update verification flow
// Implements: (a) ECDSA signature check, (b) version monotonicity, (c) dual-bank integrity.

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "aurore/security.hpp"

namespace aurore {

class FirmwareUpdater {
   public:
    struct Config {
        std::string slot_a_path = "/opt/aurore/slot_a/aurore_main";
        std::string slot_b_path = "/opt/aurore/slot_b/aurore_main";
        std::string active_link = "/opt/aurore/current";  // symlink → active slot dir
        std::string pubkey_path = "/etc/aurore/signing_key.pub";
        std::string version_path = "/etc/aurore/version";  // text file: uint32
    };

    enum class UpdateResult : int {
        SUCCESS = 0,
        SIGNATURE_INVALID = 1,
        VERSION_DOWNGRADE = 2,  // new_version <= current_version
        INTEGRITY_FAIL = 3,     // SHA-256 of staged binary != source
        IO_ERROR = 4,
    };

    explicit FirmwareUpdater(Config cfg) : cfg_(std::move(cfg)) {}

    // Read current installed version (0 if version file absent — accepts any new version).
    uint32_t current_version() const {
        std::ifstream f(cfg_.version_path);
        if (!f.is_open()) return 0;
        uint32_t v = 0;
        f >> v;
        return v;
    }

    // Return the path of the slot that is NOT currently symlinked as active.
    std::string inactive_slot_path() const {
        char target[4096] = {};
        const ssize_t n = ::readlink(cfg_.active_link.c_str(), target, sizeof(target) - 1);
        if (n > 0) {
            target[n] = '\0';
            // The active_link points to a directory; binary is inside it.
            // Compare directory prefix to determine active slot.
            const std::string active_dir(target);
            const std::string slot_a_dir = parent_dir(cfg_.slot_a_path);
            if (active_dir == slot_a_dir || active_dir == cfg_.slot_a_path) {
                return cfg_.slot_b_path;
            }
        }
        return cfg_.slot_a_path;  // default: write to slot_a if link absent or points elsewhere
    }

    // Verify installed active binary's SHA-256 matches the recorded hash in <binary>.sha256.
    bool verify_active_slot_integrity() const {
        const std::string active_bin = resolve_active_binary();
        if (active_bin.empty()) return false;
        const std::string hash_file = active_bin + ".sha256";
        const std::string recorded = read_text_file(hash_file);
        if (recorded.empty()) return false;
        const std::string actual = sha256_hex_file(active_bin);
        return !actual.empty() && actual == trim(recorded);
    }

    // Full update flow — AM7-L3-SEC-005.
    // new_binary: path to the new binary file
    // new_sig:    path to DER-encoded ECDSA P-256 signature file
    // new_version: version number embedded in the update package (must be > current)
    UpdateResult apply(const std::string& new_binary, const std::string& new_sig,
                       uint32_t new_version) {
        // (a) ECDSA signature verification
        const std::string pubkey_pem = read_text_file(cfg_.pubkey_path);
        if (pubkey_pem.empty()) {
            std::fprintf(stderr, "[FirmwareUpdater] public key not found: %s\n",
                         cfg_.pubkey_path.c_str());
            return UpdateResult::IO_ERROR;
        }
        if (!security::verify_file_ecdsa(new_binary, pubkey_pem, new_sig)) {
            std::fprintf(stderr, "[FirmwareUpdater] ECDSA signature INVALID — update rejected\n");
            return UpdateResult::SIGNATURE_INVALID;
        }

        // (b) Version monotonicity check
        const uint32_t cur_ver = current_version();
        if (new_version <= cur_ver) {
            std::fprintf(stderr,
                         "[FirmwareUpdater] version downgrade rejected: new=%u current=%u\n",
                         new_version, cur_ver);
            return UpdateResult::VERSION_DOWNGRADE;
        }

        // (c) Dual-bank: write to inactive slot and verify integrity before switching
        const std::string target_path = inactive_slot_path();
        if (!copy_file(new_binary, target_path)) {
            std::fprintf(stderr, "[FirmwareUpdater] failed to copy binary to %s: %s\n",
                         target_path.c_str(), std::strerror(errno));
            return UpdateResult::IO_ERROR;
        }

        // Verify the staged copy matches the source (integrity check)
        const std::string src_hash = sha256_hex_file(new_binary);
        const std::string dest_hash = sha256_hex_file(target_path);
        if (src_hash.empty() || dest_hash.empty() || src_hash != dest_hash) {
            std::fprintf(stderr, "[FirmwareUpdater] dual-bank integrity FAIL: src=%s staged=%s\n",
                         src_hash.c_str(), dest_hash.c_str());
            ::unlink(target_path.c_str());
            return UpdateResult::INTEGRITY_FAIL;
        }

        // Make executable
        ::chmod(target_path.c_str(), 0755);

        // Record SHA-256 alongside the staged binary for future verify_active_slot_integrity()
        write_text_file(target_path + ".sha256", dest_hash + "\n");

        // Atomic symlink swap: create a new temp link then rename over the old one
        const std::string target_dir = parent_dir(target_path);
        const std::string tmp_link = cfg_.active_link + ".tmp";
        ::unlink(tmp_link.c_str());
        if (::symlink(target_dir.c_str(), tmp_link.c_str()) != 0) {
            std::fprintf(stderr, "[FirmwareUpdater] symlink(%s) failed: %s\n", target_dir.c_str(),
                         std::strerror(errno));
            return UpdateResult::IO_ERROR;
        }
        if (::rename(tmp_link.c_str(), cfg_.active_link.c_str()) != 0) {
            std::fprintf(stderr, "[FirmwareUpdater] rename symlink failed: %s\n",
                         std::strerror(errno));
            ::unlink(tmp_link.c_str());
            return UpdateResult::IO_ERROR;
        }

        // Persist new version number
        if (!write_text_file(cfg_.version_path, std::to_string(new_version) + "\n")) {
            // Non-fatal: update succeeded, version file write failed (rollback guard weakened)
            std::fprintf(stderr, "[FirmwareUpdater] WARNING: version file write failed: %s\n",
                         cfg_.version_path.c_str());
        }

        std::fprintf(stderr, "[FirmwareUpdater] update applied: v%u → v%u slot=%s\n", cur_ver,
                     new_version, target_path.c_str());
        return UpdateResult::SUCCESS;
    }

   private:
    Config cfg_;

    static std::string parent_dir(const std::string& path) {
        const auto pos = path.rfind('/');
        if (pos == std::string::npos || pos == 0) return "/";
        return path.substr(0, pos);
    }

    std::string resolve_active_binary() const {
        char target[4096] = {};
        const ssize_t n = ::readlink(cfg_.active_link.c_str(), target, sizeof(target) - 1);
        if (n <= 0) return {};
        target[n] = '\0';
        const std::string active_dir(target);
        // Determine which slot binary is in the active directory
        if (parent_dir(cfg_.slot_a_path) == active_dir) return cfg_.slot_a_path;
        if (parent_dir(cfg_.slot_b_path) == active_dir) return cfg_.slot_b_path;
        return {};
    }

    static std::string read_text_file(const std::string& path) {
        std::ifstream f(path);
        if (!f.is_open()) return {};
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    static bool write_text_file(const std::string& path, const std::string& content) {
        std::ofstream f(path, std::ios::trunc);
        if (!f.is_open()) return false;
        f << content;
        return f.good();
    }

    static std::string sha256_hex_file(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return {};
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) return {};
        if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
            EVP_MD_CTX_free(ctx);
            return {};
        }
        char buf[65536];
        while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
            EVP_DigestUpdate(ctx, buf, static_cast<size_t>(f.gcount()));
        }
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int digest_len = 0;
        EVP_DigestFinal_ex(ctx, digest, &digest_len);
        EVP_MD_CTX_free(ctx);
        char hex[65];
        for (unsigned int i = 0; i < digest_len && i < 32U; ++i) {
            std::snprintf(hex + i * 2U, 3, "%02x", static_cast<unsigned int>(digest[i]));
        }
        return std::string(hex, 64);
    }

    static bool copy_file(const std::string& src, const std::string& dst) {
        // Ensure parent directory exists
        const std::string dst_dir = parent_dir(dst);
        ::mkdir(dst_dir.c_str(), 0755);

        std::ifstream in(src, std::ios::binary);
        if (!in.is_open()) return false;
        std::ofstream out(dst, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        char buf[65536];
        while (in.read(buf, sizeof(buf)) || in.gcount() > 0) {
            out.write(buf, in.gcount());
        }
        return out.good();
    }

    static std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        size_t end = s.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) return {};
        return s.substr(start, end - start + 1);
    }
};

}  // namespace aurore
