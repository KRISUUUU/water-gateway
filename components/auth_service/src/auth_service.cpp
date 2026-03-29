#include "auth_service/auth_service.hpp"
#include "config_store/config_store.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef HOST_TEST_BUILD
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/sha256.h"
#include <sys/time.h>
static const char* TAG = "auth_svc";
#else
#include <cstdlib>
#include <ctime>
#endif

namespace auth_service {

namespace {
int64_t now_epoch_seconds() {
#ifndef HOST_TEST_BUILD
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<int64_t>(tv.tv_sec);
#else
    return static_cast<int64_t>(std::time(nullptr));
#endif
}

bool secure_equals(const char* a, const char* b) {
    if (!a || !b) {
        return false;
    }
    size_t la = std::strlen(a);
    size_t lb = std::strlen(b);
    if (la != lb) {
        return false;
    }
    unsigned char diff = 0;
    for (size_t i = 0; i < la; ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}
} // namespace

AuthService& AuthService::instance() {
    static AuthService svc;
    return svc;
}

common::Result<void> AuthService::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        return common::Result<void>::error(common::ErrorCode::AlreadyInitialized);
    }

    auto cfg = config_store::ConfigStore::instance().config();
    session_timeout_s_ = cfg.auth.session_timeout_s;

    std::memset(&session_, 0, sizeof(session_));
    session_.valid = false;
    failed_login_count_ = 0;

    // Generate a one-time provisioning PIN for first-boot authentication.
    // Printed to serial so only someone with physical access can log in.
    if (!cfg.auth.has_password()) {
        generate_random_hex(provisioning_pin_, kProvisioningPinLength);
#ifndef HOST_TEST_BUILD
        ESP_LOGW(TAG, "============================================");
        ESP_LOGW(TAG, "  NO PASSWORD SET — first-boot mode");
        ESP_LOGW(TAG, "  Provisioning PIN: %s", provisioning_pin_);
        ESP_LOGW(TAG, "  Use this PIN to log in and set a password.");
        ESP_LOGW(TAG, "============================================");
#endif
    }

    initialized_ = true;
    return common::Result<void>::ok();
}

common::Result<SessionInfo> AuthService::login(const char* password) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        return common::Result<SessionInfo>::error(common::ErrorCode::NotInitialized);
    }
    if (!password || password[0] == '\0') {
        return common::Result<SessionInfo>::error(common::ErrorCode::InvalidArgument);
    }

    // Rate limiting: max 5 failures per 60 seconds
    const int64_t now_s = now_epoch_seconds();

    if (failed_login_count_ >= 5 && (now_s - last_failed_login_s_) < 60) {
        return common::Result<SessionInfo>::error(common::ErrorCode::AuthRateLimited);
    }

    auto cfg = config_store::ConfigStore::instance().config();

    // If no password is set, require the provisioning PIN printed to serial.
    // This ensures only someone with physical access can log in on first boot.
    bool authenticated = false;
    if (!cfg.auth.has_password()) {
        authenticated =
            (provisioning_pin_[0] != '\0') && secure_equals(password, provisioning_pin_);
    } else {
        authenticated = verify_password(password, cfg.auth.admin_password_hash);
    }

    if (!authenticated) {
        failed_login_count_++;
        last_failed_login_s_ = now_s;
#ifndef HOST_TEST_BUILD
        ESP_LOGW(TAG, "Login failed (attempt %lu)", (unsigned long)failed_login_count_);
#endif
        return common::Result<SessionInfo>::error(common::ErrorCode::AuthFailed);
    }

    // Generate session token
    failed_login_count_ = 0;
    session_.valid = true;
    session_.created_epoch_s = now_s;
    session_.expires_epoch_s = now_s + static_cast<int64_t>(session_timeout_s_);
    generate_random_hex(session_.token, kTokenLength);

#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "Login successful, session created");
#endif

    return common::Result<SessionInfo>::ok(session_);
}

common::Result<void> AuthService::logout() {
    std::lock_guard<std::mutex> lock(mutex_);
    session_.valid = false;
    std::memset(session_.token, 0, sizeof(session_.token));
    return common::Result<void>::ok();
}

bool AuthService::validate_session(const char* token) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || !session_.valid || !token) {
        return false;
    }

    // Check expiry
    const int64_t now_s = now_epoch_seconds();

    if (now_s > session_.expires_epoch_s) {
        session_.valid = false;
        std::memset(session_.token, 0, sizeof(session_.token));
        return false;
    }

    return secure_equals(token, session_.token);
}

int32_t AuthService::retry_after_seconds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (failed_login_count_ < 5) {
        return 0;
    }
    const int64_t now_s = now_epoch_seconds();
    const int64_t delta = now_s - last_failed_login_s_;
    if (delta >= 60) {
        return 0;
    }
    return static_cast<int32_t>(60 - delta);
}

// ---------------------------------------------------------------------------
// PBKDF2 helpers
// ---------------------------------------------------------------------------

bool AuthService::pbkdf2_sha256(const uint8_t* password, size_t pwd_len, const uint8_t* salt,
                                size_t salt_len, uint8_t* out) {
#ifndef HOST_TEST_BUILD
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info) {
        mbedtls_md_free(&ctx);
        return false;
    }
    if (mbedtls_md_setup(&ctx, info, 1) != 0) {
        mbedtls_md_free(&ctx);
        return false;
    }
    int rc = mbedtls_pkcs5_pbkdf2_hmac(&ctx, password, pwd_len, salt, salt_len, kPbkdf2Iterations,
                                       static_cast<uint32_t>(kPbkdf2DkBytes), out);
    mbedtls_md_free(&ctx);
    return rc == 0;
#else
    // Host test stub: deterministic fake KDF (NOT secure — test only)
    (void)pwd_len;
    (void)salt_len;
    uint32_t h = 0xdeadbeef;
    for (size_t i = 0; i < pwd_len; ++i) {
        h ^= password[i];
        h *= 0x01000193;
    }
    for (size_t i = 0; i < salt_len; ++i) {
        h ^= (salt[i] << 8);
        h *= 0x01000193;
    }
    for (size_t i = 0; i < kPbkdf2DkBytes; ++i) {
        out[i] = static_cast<uint8_t>(h ^ (i * 37));
        h = h * 1103515245 + 12345;
    }
    return true;
#endif
}

bool AuthService::is_pbkdf2_hash(const char* stored_hash) {
    return stored_hash && std::strncmp(stored_hash, "pbkdf2$", 7) == 0;
}

// ---------------------------------------------------------------------------
// Legacy SHA-256 verify: "salt_hex(32):hash_hex(64)"
// ---------------------------------------------------------------------------
bool AuthService::verify_sha256(const char* password, const char* stored_hash) {
    const char* colon = std::strchr(stored_hash, ':');
    if (!colon)
        return false;

    size_t salt_hex_len = static_cast<size_t>(colon - stored_hash);
    if (salt_hex_len != kSaltLength)
        return false;

    char salt_hex[kSaltLength + 1] = {};
    std::memcpy(salt_hex, stored_hash, salt_hex_len);
    const char* expected_hash = colon + 1;

    size_t salt_byte_len = salt_hex_len / 2;
    size_t pwd_len = std::strlen(password);

    std::vector<uint8_t> input;
    input.reserve(salt_byte_len + pwd_len);
    for (size_t i = 0; i < salt_byte_len; ++i) {
        unsigned byte_val = 0;
        std::sscanf(salt_hex + i * 2, "%2x", &byte_val);
        input.push_back(static_cast<uint8_t>(byte_val));
    }
    input.insert(input.end(), reinterpret_cast<const uint8_t*>(password),
                 reinterpret_cast<const uint8_t*>(password) + pwd_len);

    std::string computed = sha256_hex(input.data(), input.size());
    return computed == expected_hash;
}

// ---------------------------------------------------------------------------
// PBKDF2 verify: "pbkdf2$<iter>$<salt_hex>$<dk_hex>"
// ---------------------------------------------------------------------------
bool AuthService::verify_pbkdf2(const char* password, const char* stored_hash) {
    // Parse prefix
    if (!is_pbkdf2_hash(stored_hash))
        return false;

    const char* p = stored_hash + 7; // skip "pbkdf2$"

    // Parse iteration count
    char* end = nullptr;
    unsigned long iter = std::strtoul(p, &end, 10);
    if (!end || *end != '$' || iter == 0)
        return false;
    p = end + 1; // skip '$'

    // Parse salt_hex (32 chars)
    const char* dollar2 = std::strchr(p, '$');
    if (!dollar2 || (dollar2 - p) != static_cast<ptrdiff_t>(kSaltLength))
        return false;

    char salt_hex[kSaltLength + 1] = {};
    std::memcpy(salt_hex, p, kSaltLength);
    const char* expected_dk_hex = dollar2 + 1;

    // Decode salt
    uint8_t salt_bytes[kPbkdf2SaltBytes] = {};
    for (size_t i = 0; i < kPbkdf2SaltBytes; ++i) {
        unsigned byte_val = 0;
        std::sscanf(salt_hex + i * 2, "%2x", &byte_val);
        salt_bytes[i] = static_cast<uint8_t>(byte_val);
    }

    // Derive key
    uint8_t dk[kPbkdf2DkBytes] = {};
    if (!pbkdf2_sha256(reinterpret_cast<const uint8_t*>(password), std::strlen(password),
                       salt_bytes, kPbkdf2SaltBytes, dk)) {
        return false;
    }

    // Encode derived key to hex and compare
    char dk_hex[kPbkdf2DkBytes * 2 + 1] = {};
    for (size_t i = 0; i < kPbkdf2DkBytes; ++i) {
        std::sprintf(dk_hex + i * 2, "%02x", dk[i]);
    }

    return secure_equals(dk_hex, expected_dk_hex);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

common::Result<std::string> AuthService::hash_password(const char* password) {
    if (!password || password[0] == '\0') {
        return common::Result<std::string>::error(common::ErrorCode::InvalidArgument);
    }

    // Generate 16-byte random salt
    uint8_t salt_bytes[kPbkdf2SaltBytes] = {};
#ifndef HOST_TEST_BUILD
    esp_fill_random(salt_bytes, kPbkdf2SaltBytes);
#else
    for (size_t i = 0; i < kPbkdf2SaltBytes; ++i) {
        salt_bytes[i] = static_cast<uint8_t>(std::rand() & 0xFF);
    }
#endif

    char salt_hex[kSaltLength + 1] = {}; // kSaltLength == kPbkdf2SaltBytes * 2
    for (size_t i = 0; i < kPbkdf2SaltBytes; ++i) {
        std::sprintf(salt_hex + i * 2, "%02x", salt_bytes[i]);
    }

    uint8_t dk[kPbkdf2DkBytes] = {};
    if (!pbkdf2_sha256(reinterpret_cast<const uint8_t*>(password), std::strlen(password),
                       salt_bytes, kPbkdf2SaltBytes, dk)) {
        return common::Result<std::string>::error(common::ErrorCode::Unknown);
    }

    char dk_hex[kPbkdf2DkBytes * 2 + 1] = {};
    for (size_t i = 0; i < kPbkdf2DkBytes; ++i) {
        std::sprintf(dk_hex + i * 2, "%02x", dk[i]);
    }

    // Format: "pbkdf2$<iter>$<salt_hex>$<dk_hex>"
    char result[128] = {};
    std::snprintf(result, sizeof(result), "pbkdf2$%u$%s$%s", kPbkdf2Iterations, salt_hex, dk_hex);
    return common::Result<std::string>::ok(std::string(result));
}

bool AuthService::verify_password(const char* password, const char* stored_hash) {
    if (!password || !stored_hash)
        return false;
    if (is_pbkdf2_hash(stored_hash)) {
        return verify_pbkdf2(password, stored_hash);
    }
    // Legacy path: SHA-256 "salt_hex:hash_hex"
    return verify_sha256(password, stored_hash);
}

void AuthService::generate_random_hex(char* out, size_t hex_len) {
    size_t byte_len = hex_len / 2;
    uint8_t bytes[64] = {};
    if (byte_len > sizeof(bytes))
        byte_len = sizeof(bytes);

#ifndef HOST_TEST_BUILD
    esp_fill_random(bytes, byte_len);
#else
    for (size_t i = 0; i < byte_len; ++i) {
        bytes[i] = static_cast<uint8_t>(std::rand() & 0xFF);
    }
#endif

    for (size_t i = 0; i < byte_len; ++i) {
        std::sprintf(out + i * 2, "%02x", bytes[i]);
    }
    out[hex_len] = '\0';
}

std::string AuthService::sha256_hex(const uint8_t* data, size_t len) {
    uint8_t hash[32] = {};

#ifndef HOST_TEST_BUILD
    mbedtls_sha256(data, len, hash, 0);
#else
    // Simple deterministic hash for testing (NOT cryptographically secure)
    uint32_t h = 0x811c9dc5;
    for (size_t i = 0; i < len; ++i) {
        h ^= data[i];
        h *= 0x01000193;
    }
    for (int i = 0; i < 32; ++i) {
        hash[i] = static_cast<uint8_t>((h >> ((i % 4) * 8)) ^ (i * 37));
        h = h * 1103515245 + 12345;
    }
#endif

    char hex[65] = {};
    for (int i = 0; i < 32; ++i) {
        std::sprintf(hex + i * 2, "%02x", hash[i]);
    }
    return std::string(hex);
}

} // namespace auth_service
