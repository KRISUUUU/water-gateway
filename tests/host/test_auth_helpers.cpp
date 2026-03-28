#include "host_test_stubs.hpp"
#include "config_store/config_models.hpp"
#include "config_store/config_store.hpp"
#include "auth_service/auth_service.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

using namespace auth_service;

static void test_hash_password() {
    auto result = AuthService::hash_password("mypassword");
    assert(result.is_ok());
    std::string hash = result.value();
    // New format: "pbkdf2$<iter>$<salt_hex32>$<dk_hex64>"
    assert(hash.rfind("pbkdf2$", 0) == 0);
    // Must fit in admin_password_hash field (128 bytes)
    assert(hash.length() < 128);
    printf("  PASS: hash_password produces PBKDF2 format (len=%zu)\n", hash.length());
}

// ---------------------------------------------------------------------------
// Legacy SHA-256 hash helpers (mirror of the old hash_password() algorithm)
// Used only for generating known-good legacy test vectors.
// Must stay in sync with auth_service.cpp verify_sha256() interpretation.
// ---------------------------------------------------------------------------

// Deterministic fake SHA-256: identical to the HOST_TEST_BUILD stub in auth_service.cpp.
static std::string legacy_sha256_hex(const uint8_t* data, size_t len) {
    uint8_t hash[32] = {};
    uint32_t h = 0x811c9dc5;
    for (size_t i = 0; i < len; ++i) {
        h ^= data[i];
        h *= 0x01000193;
    }
    for (int i = 0; i < 32; ++i) {
        hash[i] = static_cast<uint8_t>((h >> ((i % 4) * 8)) ^ (i * 37));
        h = h * 1103515245 + 12345;
    }
    char hex[65] = {};
    for (int i = 0; i < 32; ++i) {
        std::sprintf(hex + i * 2, "%02x", hash[i]);
    }
    return std::string(hex);
}

// Build a legacy "salt_hex(32):sha256_hex(64)" hash for the given salt and password.
static std::string make_legacy_hash(const char* salt_hex32, const char* password) {
    uint8_t salt_bytes[16] = {};
    for (int i = 0; i < 16; ++i) {
        unsigned v = 0;
        std::sscanf(salt_hex32 + i * 2, "%2x", &v);
        salt_bytes[i] = static_cast<uint8_t>(v);
    }
    size_t pwd_len = std::strlen(password);
    std::vector<uint8_t> input(salt_bytes, salt_bytes + 16);
    input.insert(input.end(), reinterpret_cast<const uint8_t*>(password),
                 reinterpret_cast<const uint8_t*>(password) + pwd_len);
    return std::string(salt_hex32) + ":" + legacy_sha256_hex(input.data(), input.size());
}

static void test_hash_password_legacy_still_verifies() {
    // Build a real legacy SHA-256 hash using the same deterministic stub that
    // auth_service verify_sha256() uses internally (HOST_TEST_BUILD path).
    // Fixed salt: 16 zero bytes → salt_hex = "00000000000000000000000000000000"
    const char* salt_hex = "00000000000000000000000000000000";
    const char* password = "legacypassword";
    std::string legacy_hash = make_legacy_hash(salt_hex, password);

    // Confirm format looks like a legacy hash
    assert(legacy_hash.length() == 97);
    assert(legacy_hash[32] == ':');

    // verify_password() must accept the correct password via legacy path
    bool ok = AuthService::verify_password(password, legacy_hash.c_str());
    assert(ok);

    // verify_password() must reject a wrong password via legacy path
    bool reject = AuthService::verify_password("wrongpassword", legacy_hash.c_str());
    assert(!reject);

    printf("  PASS: legacy SHA-256 hash verifies correct password and rejects wrong one\n");
}

static void test_verify_correct_password() {
    auto hash_result = AuthService::hash_password("secret123");
    assert(hash_result.is_ok());

    bool verified = AuthService::verify_password("secret123", hash_result.value().c_str());
    assert(verified);
    printf("  PASS: correct password verifies\n");
}

static void test_verify_wrong_password() {
    auto hash_result = AuthService::hash_password("secret123");
    assert(hash_result.is_ok());

    bool verified = AuthService::verify_password("wrong", hash_result.value().c_str());
    assert(!verified);
    printf("  PASS: wrong password rejected\n");
}

static void test_hash_empty_password_fails() {
    auto result = AuthService::hash_password("");
    assert(result.is_error());
    assert(result.error() == common::ErrorCode::InvalidArgument);
    printf("  PASS: empty password hash fails\n");
}

static void test_hash_null_password_fails() {
    auto result = AuthService::hash_password(nullptr);
    assert(result.is_error());
    printf("  PASS: null password hash fails\n");
}

static void test_verify_null_stored_hash() {
    assert(!AuthService::verify_password("test", nullptr));
    printf("  PASS: null stored hash returns false\n");
}

static void test_verify_invalid_format() {
    assert(!AuthService::verify_password("test", "not-a-valid-hash"));
    printf("  PASS: invalid hash format returns false\n");
}

static void test_verify_invalid_pbkdf2_variants() {
    assert(!AuthService::verify_password(
        "test", "pbkdf2$0$00000000000000000000000000000000$abcdef"));
    assert(!AuthService::verify_password(
        "test", "pbkdf2$600000$000000000000000000000000000000$abcdef"));
    assert(!AuthService::verify_password(
        "test",
        "pbkdf2$600000$00000000000000000000000000000000$abcd"));
    printf("  PASS: malformed PBKDF2 hashes are rejected\n");
}

static void test_verify_invalid_legacy_hash_variants() {
    assert(!AuthService::verify_password("test", "0011:abcd"));
    assert(!AuthService::verify_password(
        "test", "00000000000000000000000000000000:"));
    assert(!AuthService::verify_password(
        "test", "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz:1234"));
    printf("  PASS: malformed legacy hashes are rejected\n");
}

static void test_different_passwords_different_hashes() {
    auto h1 = AuthService::hash_password("password1");
    auto h2 = AuthService::hash_password("password2");
    assert(h1.is_ok() && h2.is_ok());
    assert(h1.value() != h2.value());
    printf("  PASS: different passwords produce different hashes\n");
}

int main() {
    printf("=== test_auth_helpers ===\n");

    // Init config store for auth service dependency
    config_store::ConfigStore::instance().initialize();

    test_hash_password();
    test_hash_password_legacy_still_verifies();
    test_verify_correct_password();
    test_verify_wrong_password();
    test_hash_empty_password_fails();
    test_hash_null_password_fails();
    test_verify_null_stored_hash();
    test_verify_invalid_format();
    test_verify_invalid_pbkdf2_variants();
    test_verify_invalid_legacy_hash_variants();
    test_different_passwords_different_hashes();
    printf("All auth helper tests passed.\n");
    return 0;
}
