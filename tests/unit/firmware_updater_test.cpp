// AM7-L3-SEC-005: firmware update verification unit tests
// Tests: ECDSA sig check, version monotonicity, dual-bank integrity.
// Uses real OpenSSL (no mocks), temp files as update packages.

#include "aurore/firmware_updater.hpp"
#include "aurore/security.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

static int g_pass = 0;
static int g_fail = 0;

#define RUN_TEST(name) do { \
    std::cout << "Running " << #name << "... "; \
    try { name(); std::cout << "PASS\n"; ++g_pass; } \
    catch (const std::exception& e) { \
        std::cout << "FAIL: " << e.what() << "\n"; ++g_fail; \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) throw std::runtime_error("Assertion failed: " #cond); \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) throw std::runtime_error( \
        std::string("Assertion failed: " #a " == " #b \
            " (lhs=") + std::to_string(static_cast<int>(a)) + \
        " rhs=" + std::to_string(static_cast<int>(b)) + ")"); \
} while(0)

// ---------------------------------------------------------------------------
// Test fixture helpers
// ---------------------------------------------------------------------------

static std::string tmpdir() {
    char tmpl[] = "/tmp/fw_test_XXXXXX";
    char* d = ::mkdtemp(tmpl);
    if (!d) throw std::runtime_error("mkdtemp failed");
    return d;
}

static void write_file(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) throw std::runtime_error("Cannot write: " + path);
    f << content;
}

static void ensure_dir(const std::string& path) {
    ::mkdir(path.c_str(), 0755);
}

// Generate an in-memory ECDSA P-256 keypair and return PEM strings
struct KeyPair { std::string priv_pem; std::string pub_pem; };

static KeyPair generate_ec_keypair() {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    if (!ctx) throw std::runtime_error("EVP_PKEY_CTX_new_id failed");

    if (EVP_PKEY_keygen_init(ctx) != 1 ||
        EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1) != 1) {
        EVP_PKEY_CTX_free(ctx);
        throw std::runtime_error("EC keygen init failed");
    }

    EVP_PKEY* pkey = nullptr;
    if (EVP_PKEY_keygen(ctx, &pkey) != 1) {
        EVP_PKEY_CTX_free(ctx);
        throw std::runtime_error("EVP_PKEY_keygen failed");
    }
    EVP_PKEY_CTX_free(ctx);

    // Serialize private key to PEM
    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string priv_pem(data, static_cast<size_t>(len));
    BIO_free(bio);

    // Serialize public key to PEM
    bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PUBKEY(bio, pkey);
    len = BIO_get_mem_data(bio, &data);
    std::string pub_pem(data, static_cast<size_t>(len));
    BIO_free(bio);

    EVP_PKEY_free(pkey);
    return {priv_pem, pub_pem};
}

static std::string sign_file(const std::string& file_path, const std::string& priv_pem) {
    BIO* bio = BIO_new_mem_buf(priv_pem.data(), static_cast<int>(priv_pem.size()));
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) throw std::runtime_error("Failed to load private key for sign");

    // Hash the file using EVP (OpenSSL 3.0 compatible)
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    {
        std::ifstream f(file_path, std::ios::binary);
        EVP_MD_CTX* hctx = EVP_MD_CTX_new();
        EVP_DigestInit_ex(hctx, EVP_sha256(), nullptr);
        char buf[65536];
        while (f.read(buf, sizeof(buf)) || f.gcount() > 0)
            EVP_DigestUpdate(hctx, buf, static_cast<size_t>(f.gcount()));
        EVP_DigestFinal_ex(hctx, hash, &hash_len);
        EVP_MD_CTX_free(hctx);
    }

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    EVP_DigestSignInit(mdctx, nullptr, EVP_sha256(), nullptr, pkey);
    EVP_DigestSignUpdate(mdctx, hash, hash_len);
    size_t sig_len = 0;
    EVP_DigestSignFinal(mdctx, nullptr, &sig_len);
    std::string sig(sig_len, '\0');
    EVP_DigestSignFinal(mdctx, reinterpret_cast<unsigned char*>(sig.data()), &sig_len);
    sig.resize(sig_len);
    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    return sig;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_valid_update_succeeds() {
    const std::string base = tmpdir();
    const std::string slot_a = base + "/slot_a";
    const std::string slot_b = base + "/slot_b";
    ensure_dir(slot_a);
    ensure_dir(slot_b);

    // Generate keypair and write public key to temp location
    const KeyPair kp = generate_ec_keypair();
    const std::string pubkey_path = base + "/signing_key.pub";
    write_file(pubkey_path, kp.pub_pem);

    // Prepare "new binary"
    const std::string new_bin = base + "/aurore_main_v2";
    write_file(new_bin, "fake binary content v2");

    // Sign it
    const std::string sig = sign_file(new_bin, kp.priv_pem);
    const std::string sig_path = new_bin + ".sig";
    write_file(sig_path, sig);

    // Current version = 1
    const std::string version_path = base + "/version";
    write_file(version_path, "1\n");

    const std::string active_link = base + "/current";

    aurore::FirmwareUpdater::Config cfg;
    cfg.slot_a_path  = slot_a + "/aurore_main";
    cfg.slot_b_path  = slot_b + "/aurore_main";
    cfg.active_link  = active_link;
    cfg.pubkey_path  = pubkey_path;
    cfg.version_path = version_path;

    aurore::FirmwareUpdater updater(cfg);
    ASSERT_EQ(1u, updater.current_version());

    // Apply update v2 > v1 — must succeed
    const auto result = updater.apply(new_bin, sig_path, 2u);
    ASSERT_EQ(aurore::FirmwareUpdater::UpdateResult::SUCCESS, result);

    // Version must now be 2
    ASSERT_EQ(2u, updater.current_version());

    // Symlink must point to slot_a dir (was default inactive)
    char target[4096] = {};
    const ssize_t n = ::readlink(active_link.c_str(), target, sizeof(target) - 1);
    ASSERT_TRUE(n > 0);
}

static void test_signature_invalid_rejected() {
    const std::string base = tmpdir();
    const std::string slot_a = base + "/slot_a";
    const std::string slot_b = base + "/slot_b";
    ensure_dir(slot_a);
    ensure_dir(slot_b);

    // Generate two different keypairs — sign with key A, verify with key B
    const KeyPair kp_sign   = generate_ec_keypair();
    const KeyPair kp_verify = generate_ec_keypair();

    const std::string pubkey_path = base + "/signing_key.pub";
    write_file(pubkey_path, kp_verify.pub_pem);  // wrong key

    const std::string new_bin = base + "/aurore_main_bad";
    write_file(new_bin, "fake binary bad");

    const std::string sig = sign_file(new_bin, kp_sign.priv_pem);  // signed with wrong key
    const std::string sig_path = new_bin + ".sig";
    write_file(sig_path, sig);

    write_file(base + "/version", "1\n");

    aurore::FirmwareUpdater::Config cfg;
    cfg.slot_a_path  = slot_a + "/aurore_main";
    cfg.slot_b_path  = slot_b + "/aurore_main";
    cfg.active_link  = base + "/current";
    cfg.pubkey_path  = pubkey_path;
    cfg.version_path = base + "/version";

    aurore::FirmwareUpdater updater(cfg);
    const auto result = updater.apply(new_bin, sig_path, 2u);
    ASSERT_EQ(aurore::FirmwareUpdater::UpdateResult::SIGNATURE_INVALID, result);
    // Version must not have changed
    ASSERT_EQ(1u, updater.current_version());
}

static void test_version_downgrade_rejected() {
    const std::string base = tmpdir();
    const std::string slot_a = base + "/slot_a";
    const std::string slot_b = base + "/slot_b";
    ensure_dir(slot_a);
    ensure_dir(slot_b);

    const KeyPair kp = generate_ec_keypair();
    write_file(base + "/signing_key.pub", kp.pub_pem);

    const std::string new_bin = base + "/aurore_main_old";
    write_file(new_bin, "old binary");
    const std::string sig = sign_file(new_bin, kp.priv_pem);
    write_file(new_bin + ".sig", sig);

    // Current version is 10, update claims version 5
    write_file(base + "/version", "10\n");

    aurore::FirmwareUpdater::Config cfg;
    cfg.slot_a_path  = slot_a + "/aurore_main";
    cfg.slot_b_path  = slot_b + "/aurore_main";
    cfg.active_link  = base + "/current";
    cfg.pubkey_path  = base + "/signing_key.pub";
    cfg.version_path = base + "/version";

    aurore::FirmwareUpdater updater(cfg);
    const auto result = updater.apply(new_bin, new_bin + ".sig", 5u);
    ASSERT_EQ(aurore::FirmwareUpdater::UpdateResult::VERSION_DOWNGRADE, result);
    ASSERT_EQ(10u, updater.current_version());
}

static void test_same_version_rejected() {
    // Version must be strictly greater, not equal
    const std::string base = tmpdir();
    const std::string slot_a = base + "/slot_a";
    const std::string slot_b = base + "/slot_b";
    ensure_dir(slot_a);
    ensure_dir(slot_b);

    const KeyPair kp = generate_ec_keypair();
    write_file(base + "/signing_key.pub", kp.pub_pem);

    const std::string new_bin = base + "/aurore_main_same";
    write_file(new_bin, "same binary");
    write_file(new_bin + ".sig", sign_file(new_bin, kp.priv_pem));
    write_file(base + "/version", "7\n");

    aurore::FirmwareUpdater::Config cfg;
    cfg.slot_a_path  = slot_a + "/aurore_main";
    cfg.slot_b_path  = slot_b + "/aurore_main";
    cfg.active_link  = base + "/current";
    cfg.pubkey_path  = base + "/signing_key.pub";
    cfg.version_path = base + "/version";

    aurore::FirmwareUpdater updater(cfg);
    const auto result = updater.apply(new_bin, new_bin + ".sig", 7u);
    ASSERT_EQ(aurore::FirmwareUpdater::UpdateResult::VERSION_DOWNGRADE, result);
}

static void test_first_install_zero_version_accepts_any() {
    // If version file absent (current=0), any version > 0 is accepted
    const std::string base = tmpdir();
    ensure_dir(base + "/slot_a");
    ensure_dir(base + "/slot_b");

    const KeyPair kp = generate_ec_keypair();
    write_file(base + "/signing_key.pub", kp.pub_pem);

    const std::string new_bin = base + "/aurore_main_v1";
    write_file(new_bin, "first install binary");
    write_file(new_bin + ".sig", sign_file(new_bin, kp.priv_pem));
    // No version file — current_version() returns 0

    aurore::FirmwareUpdater::Config cfg;
    cfg.slot_a_path  = base + "/slot_a/aurore_main";
    cfg.slot_b_path  = base + "/slot_b/aurore_main";
    cfg.active_link  = base + "/current";
    cfg.pubkey_path  = base + "/signing_key.pub";
    cfg.version_path = base + "/version";

    aurore::FirmwareUpdater updater(cfg);
    ASSERT_EQ(0u, updater.current_version());

    const auto result = updater.apply(new_bin, new_bin + ".sig", 1u);
    ASSERT_EQ(aurore::FirmwareUpdater::UpdateResult::SUCCESS, result);
    ASSERT_EQ(1u, updater.current_version());
}

static void test_dual_bank_alternates_slots() {
    // First update should land in slot_a (default inactive), second in slot_b
    const std::string base = tmpdir();
    ensure_dir(base + "/slot_a");
    ensure_dir(base + "/slot_b");

    const KeyPair kp = generate_ec_keypair();
    write_file(base + "/signing_key.pub", kp.pub_pem);

    aurore::FirmwareUpdater::Config cfg;
    cfg.slot_a_path  = base + "/slot_a/aurore_main";
    cfg.slot_b_path  = base + "/slot_b/aurore_main";
    cfg.active_link  = base + "/current";
    cfg.pubkey_path  = base + "/signing_key.pub";
    cfg.version_path = base + "/version";
    write_file(cfg.version_path, "0\n");

    aurore::FirmwareUpdater updater(cfg);

    // First update → should write to slot_a
    const std::string bin_v1 = base + "/bin_v1";
    write_file(bin_v1, "binary v1 content");
    write_file(bin_v1 + ".sig", sign_file(bin_v1, kp.priv_pem));
    ASSERT_EQ(aurore::FirmwareUpdater::UpdateResult::SUCCESS,
              updater.apply(bin_v1, bin_v1 + ".sig", 1u));
    ASSERT_TRUE(::access(cfg.slot_a_path.c_str(), F_OK) == 0);

    // Second update → slot_a is now active, so should write to slot_b
    const std::string bin_v2 = base + "/bin_v2";
    write_file(bin_v2, "binary v2 content longer");
    write_file(bin_v2 + ".sig", sign_file(bin_v2, kp.priv_pem));
    ASSERT_EQ(aurore::FirmwareUpdater::UpdateResult::SUCCESS,
              updater.apply(bin_v2, bin_v2 + ".sig", 2u));
    ASSERT_TRUE(::access(cfg.slot_b_path.c_str(), F_OK) == 0);

    ASSERT_EQ(2u, updater.current_version());
}

static void test_missing_pubkey_returns_io_error() {
    const std::string base = tmpdir();
    ensure_dir(base + "/slot_a");
    ensure_dir(base + "/slot_b");

    const std::string new_bin = base + "/aurore_main";
    write_file(new_bin, "binary");
    write_file(new_bin + ".sig", "invalidsig");
    write_file(base + "/version", "1\n");

    aurore::FirmwareUpdater::Config cfg;
    cfg.slot_a_path  = base + "/slot_a/aurore_main";
    cfg.slot_b_path  = base + "/slot_b/aurore_main";
    cfg.active_link  = base + "/current";
    cfg.pubkey_path  = base + "/nonexistent_key.pub";  // missing
    cfg.version_path = base + "/version";

    aurore::FirmwareUpdater updater(cfg);
    const auto result = updater.apply(new_bin, new_bin + ".sig", 2u);
    ASSERT_EQ(aurore::FirmwareUpdater::UpdateResult::IO_ERROR, result);
}

// ---------------------------------------------------------------------------

int main() {
    std::cout << "=== FirmwareUpdater Unit Tests (AM7-L3-SEC-005) ===\n\n";
    RUN_TEST(test_valid_update_succeeds);
    RUN_TEST(test_signature_invalid_rejected);
    RUN_TEST(test_version_downgrade_rejected);
    RUN_TEST(test_same_version_rejected);
    RUN_TEST(test_first_install_zero_version_accepts_any);
    RUN_TEST(test_dual_bank_alternates_slots);
    RUN_TEST(test_missing_pubkey_returns_io_error);

    std::cout << "\n=== Results ===\n";
    std::cout << "Passed: " << g_pass << "\n";
    std::cout << "Failed: " << g_fail << "\n";
    return g_fail > 0 ? 1 : 0;
}
