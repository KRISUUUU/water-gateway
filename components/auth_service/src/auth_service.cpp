#include "auth_service/auth_service.hpp"
#include "config_store/config_store.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef HOST_TEST_BUILD
#include "esp_log.h"
#include "esp_random.h"
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
#ifndef HOST_TEST_BUILD
        const int64_t retry_after = 60 - (now_s - last_failed_login_s_);
        ESP_LOGW(TAG, "Login rate-limited (failed=%lu retry_after=%llds)",
                 (unsigned long)failed_login_count_, static_cast<long long>(retry_after));
#endif
        return common::Result<SessionInfo>::error(common::ErrorCode::AuthRateLimited);
    }

    auto cfg = config_store::ConfigStore::instance().config();

    // If no password is configured, only allow bootstrap login while provisioning
    // mode is active (WiFi credentials not set). Reject passwordless login in
    // normal runtime to reduce takeover risk.
    bool authenticated = false;
    if (!cfg.auth.has_password()) {
        const bool provisioning_mode = !cfg.wifi.is_configured();
        if (provisioning_mode) {
            authenticated = true;
#ifndef HOST_TEST_BUILD
            ESP_LOGW(TAG, "Passwordless bootstrap login accepted in provisioning mode");
#endif
        } else {
            authenticated = false;
#ifndef HOST_TEST_BUILD
            ESP_LOGE(TAG,
                     "Passwordless login rejected: admin password missing outside provisioning mode");
#endif
        }
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

common::Result<std::string> AuthService::hash_password(const char* password) {
    if (!password || password[0] == '\0') {
        return common::Result<std::string>::error(common::ErrorCode::InvalidArgument);
    }

    // Generate random salt
    char salt_hex[kSaltLength + 1] = {};
    generate_random_hex(salt_hex, kSaltLength);

    // Compute SHA-256(salt_bytes + password)
    size_t salt_byte_len = kSaltLength / 2;
    size_t pwd_len = std::strlen(password);
    size_t total_len = salt_byte_len + pwd_len;

    uint8_t* input = new uint8_t[total_len];

    // Convert salt hex back to bytes for hashing
    for (size_t i = 0; i < salt_byte_len; ++i) {
        unsigned byte_val = 0;
        std::sscanf(salt_hex + i * 2, "%2x", &byte_val);
        input[i] = static_cast<uint8_t>(byte_val);
    }
    std::memcpy(input + salt_byte_len, password, pwd_len);

    std::string hash_hex = sha256_hex(input, total_len);
    delete[] input;

    // Format: "salt_hex:hash_hex"
    std::string result = std::string(salt_hex) + ":" + hash_hex;
    return common::Result<std::string>::ok(std::move(result));
}

bool AuthService::verify_password(const char* password, const char* stored_hash) {
    if (!password || !stored_hash)
        return false;

    // Parse "salt_hex:hash_hex"
    const char* colon = std::strchr(stored_hash, ':');
    if (!colon)
        return false;

    size_t salt_hex_len = static_cast<size_t>(colon - stored_hash);
    if (salt_hex_len != kSaltLength)
        return false;

    char salt_hex[kSaltLength + 1] = {};
    std::memcpy(salt_hex, stored_hash, salt_hex_len);

    const char* expected_hash = colon + 1;

    // Recompute SHA-256(salt_bytes + password)
    size_t salt_byte_len = salt_hex_len / 2;
    size_t pwd_len = std::strlen(password);
    size_t total_len = salt_byte_len + pwd_len;

    std::vector<uint8_t> input;
    input.reserve(total_len);

    for (size_t i = 0; i < salt_byte_len; ++i) {
        unsigned byte_val = 0;
        std::sscanf(salt_hex + i * 2, "%2x", &byte_val);
        input.push_back(static_cast<uint8_t>(byte_val));
    }
    input.insert(input.end(), reinterpret_cast<const uint8_t*>(password),
                 reinterpret_cast<const uint8_t*>(password) + pwd_len);

    std::string computed_hash = sha256_hex(input.data(), input.size());

    return secure_equals(computed_hash.c_str(), expected_hash);
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
