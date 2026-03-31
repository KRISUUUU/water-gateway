#include "auth_service/auth_service.hpp"
#include "config_store/config_store.hpp"
#include <cstdio>
#include <cstring>
#include <memory>
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
constexpr size_t kLegacySaltLength = 32; // Hex-encoded 16 bytes
constexpr uint32_t kPbkdf2Iterations = 2000;
constexpr const char* kPbkdf2Prefix = "pbkdf2$";
constexpr uint32_t kLoginRateLimitFailures = 5;
constexpr int64_t kLoginRateLimitWindowS = 60;

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

bool hex_to_bytes(const char* hex, size_t hex_len, std::vector<uint8_t>& out) {
    if (!hex || (hex_len % 2) != 0) {
        return false;
    }

    out.clear();
    out.reserve(hex_len / 2);
    for (size_t i = 0; i < hex_len; i += 2) {
        unsigned byte_val = 0;
        if (std::sscanf(hex + i, "%2x", &byte_val) != 1) {
            return false;
        }
        out.push_back(static_cast<uint8_t>(byte_val));
    }
    return true;
}

void bytes_to_hex(const uint8_t* data, size_t len, char* out, size_t out_size) {
    if (!out || out_size == 0) {
        return;
    }

    if (!data || out_size < (len * 2U + 1U)) {
        out[0] = '\0';
        return;
    }

    for (size_t i = 0; i < len; ++i) {
        const size_t offset = i * 2U;
        std::snprintf(out + offset, out_size - offset, "%02x", data[i]);
    }
    out[len * 2U] = '\0';
}

void sha256_bytes(const uint8_t* data, size_t len, uint8_t hash[32]) {
#ifndef HOST_TEST_BUILD
    mbedtls_sha256(data, len, hash, 0);
#else
    // Deterministic host-test fallback (NOT cryptographically secure).
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
}

bool derive_pbkdf2_sha256(const char* password, const uint8_t* salt, size_t salt_len,
                          uint32_t iterations, uint8_t out[32]) {
    if (!password || !salt || !out || iterations == 0) {
        return false;
    }

#ifndef HOST_TEST_BUILD
    const auto* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md_info) {
        return false;
    }

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    if (mbedtls_md_setup(&ctx, md_info, 1) != 0) {
        mbedtls_md_free(&ctx);
        return false;
    }

    const int rc =
        mbedtls_pkcs5_pbkdf2_hmac(&ctx, reinterpret_cast<const unsigned char*>(password),
                                  std::strlen(password), salt, salt_len, iterations, 32, out);
    mbedtls_md_free(&ctx);
    return rc == 0;
#else
    const size_t pwd_len = std::strlen(password);
    std::vector<uint8_t> state;
    state.reserve(salt_len + pwd_len + sizeof(iterations) + 32);
    state.insert(state.end(), salt, salt + salt_len);
    state.insert(state.end(), reinterpret_cast<const uint8_t*>(password),
                 reinterpret_cast<const uint8_t*>(password) + pwd_len);

    uint8_t block[32]{};
    sha256_bytes(state.data(), state.size(), block);
    std::memcpy(out, block, sizeof(block));

    for (uint32_t iter = 1; iter < iterations; ++iter) {
        std::vector<uint8_t> round;
        round.reserve(sizeof(block) + salt_len + pwd_len + sizeof(iter));
        round.insert(round.end(), block, block + sizeof(block));
        round.insert(round.end(), salt, salt + salt_len);
        round.insert(round.end(), reinterpret_cast<const uint8_t*>(password),
                     reinterpret_cast<const uint8_t*>(password) + pwd_len);
        round.push_back(static_cast<uint8_t>(iter & 0xFF));
        round.push_back(static_cast<uint8_t>((iter >> 8) & 0xFF));
        round.push_back(static_cast<uint8_t>((iter >> 16) & 0xFF));
        round.push_back(static_cast<uint8_t>((iter >> 24) & 0xFF));
        sha256_bytes(round.data(), round.size(), block);
        for (size_t i = 0; i < sizeof(block); ++i) {
            out[i] ^= block[i];
        }
    }
    return true;
#endif
}

bool verify_legacy_sha256_password(const char* password, const char* stored_hash) {
    const char* colon = std::strchr(stored_hash, ':');
    if (!colon) {
        return false;
    }

    const size_t salt_hex_len = static_cast<size_t>(colon - stored_hash);
    if (salt_hex_len != kLegacySaltLength) {
        return false;
    }

    std::vector<uint8_t> salt_bytes;
    if (!hex_to_bytes(stored_hash, salt_hex_len, salt_bytes)) {
        return false;
    }

    const char* expected_hash = colon + 1;
    const size_t pwd_len = std::strlen(password);
    std::vector<uint8_t> input;
    input.reserve(salt_bytes.size() + pwd_len);
    input.insert(input.end(), salt_bytes.begin(), salt_bytes.end());
    input.insert(input.end(), reinterpret_cast<const uint8_t*>(password),
                 reinterpret_cast<const uint8_t*>(password) + pwd_len);

    uint8_t hash[32]{};
    sha256_bytes(input.data(), input.size(), hash);
    char computed_hash[kHashLength + 1] = {};
    bytes_to_hex(hash, sizeof(hash), computed_hash, sizeof(computed_hash));
    const bool ok = secure_equals(computed_hash, expected_hash);
#ifndef HOST_TEST_BUILD
    if (ok) {
        ESP_LOGW(TAG, "Legacy salted SHA-256 password hash accepted; upgrade to PBKDF2 recommended");
    }
#endif
    return ok;
}

bool record_matches_client(const LoginAttemptRecord& record, uint32_t client_id) {
    return record.fail_count > 0 && record.client_id == client_id;
}

void clear_record(LoginAttemptRecord& record) {
    record.client_id = 0;
    record.fail_count = 0;
    record.last_fail_s = 0;
}

size_t find_record_index(const LoginAttemptRecord* records, uint32_t client_id) {
    for (size_t i = 0; i < AuthService::kMaxTrackedClients; ++i) {
        if (record_matches_client(records[i], client_id)) {
            return i;
        }
    }
    return AuthService::kMaxTrackedClients;
}

size_t claim_record_index(LoginAttemptRecord* records, uint32_t client_id) {
    const size_t existing = find_record_index(records, client_id);
    if (existing != AuthService::kMaxTrackedClients) {
        return existing;
    }

    for (size_t i = 0; i < AuthService::kMaxTrackedClients; ++i) {
        if (records[i].fail_count == 0) {
            records[i].client_id = client_id;
            return i;
        }
    }

    size_t oldest = 0;
    for (size_t i = 1; i < AuthService::kMaxTrackedClients; ++i) {
        if (records[i].last_fail_s < records[oldest].last_fail_s) {
            oldest = i;
        }
    }
    records[oldest].client_id = client_id;
    records[oldest].fail_count = 0;
    records[oldest].last_fail_s = 0;
    return oldest;
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
    for (auto& record : records_) {
        clear_record(record);
    }

    initialized_ = true;
    return common::Result<void>::ok();
}

common::Result<SessionInfo> AuthService::login(const char* password, uint32_t client_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        return common::Result<SessionInfo>::error(common::ErrorCode::NotInitialized);
    }
    if (!password || password[0] == '\0') {
        return common::Result<SessionInfo>::error(common::ErrorCode::InvalidArgument);
    }

    // Rate limiting: max 5 failures per 60 seconds
    const int64_t now_s = now_epoch_seconds();
    const size_t record_idx = find_record_index(records_, client_id);
    if (record_idx != kMaxTrackedClients) {
        LoginAttemptRecord& record = records_[record_idx];
        const int64_t delta = now_s - record.last_fail_s;
        if (record.fail_count >= kLoginRateLimitFailures && delta < kLoginRateLimitWindowS) {
#ifndef HOST_TEST_BUILD
            const int64_t retry_after = kLoginRateLimitWindowS - delta;
            ESP_LOGW(TAG, "Login rate-limited for client %lu (failed=%lu retry_after=%llds)",
                     static_cast<unsigned long>(client_id),
                     static_cast<unsigned long>(record.fail_count),
                     static_cast<long long>(retry_after));
#endif
            return common::Result<SessionInfo>::error(common::ErrorCode::AuthRateLimited);
        }
        if (delta >= kLoginRateLimitWindowS) {
            clear_record(record);
        }
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
        LoginAttemptRecord& record = records_[claim_record_index(records_, client_id)];
        record.client_id = client_id;
        record.fail_count++;
        record.last_fail_s = now_s;
#ifndef HOST_TEST_BUILD
        ESP_LOGW(TAG, "Login failed for client %lu (attempt %lu)",
                 static_cast<unsigned long>(client_id),
                 static_cast<unsigned long>(record.fail_count));
#endif
        return common::Result<SessionInfo>::error(common::ErrorCode::AuthFailed);
    }

    // Generate session token
    const size_t success_idx = find_record_index(records_, client_id);
    if (success_idx != kMaxTrackedClients) {
        clear_record(records_[success_idx]);
    }
    session_timeout_s_ = cfg.auth.session_timeout_s;
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

int32_t AuthService::retry_after_seconds(uint32_t client_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t record_idx = find_record_index(records_, client_id);
    if (record_idx == kMaxTrackedClients) {
        return 0;
    }
    const LoginAttemptRecord& record = records_[record_idx];
    if (record.fail_count < kLoginRateLimitFailures) {
        return 0;
    }
    const int64_t now_s = now_epoch_seconds();
    const int64_t delta = now_s - record.last_fail_s;
    if (delta >= kLoginRateLimitWindowS) {
        return 0;
    }
    return static_cast<int32_t>(kLoginRateLimitWindowS - delta);
}

#ifdef HOST_TEST_BUILD
bool AuthService::set_attempt_record_for_test(uint32_t client_id, uint32_t fail_count,
                                              int64_t last_fail_s) {
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t existing = find_record_index(records_, client_id);
    if (fail_count == 0) {
        if (existing != kMaxTrackedClients) {
            clear_record(records_[existing]);
        }
        return true;
    }

    LoginAttemptRecord& record = records_[claim_record_index(records_, client_id)];
    record.client_id = client_id;
    record.fail_count = fail_count;
    record.last_fail_s = last_fail_s;
    return true;
}

bool AuthService::has_attempt_record_for_test(uint32_t client_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return find_record_index(records_, client_id) != kMaxTrackedClients;
}
#endif

common::Result<std::string> AuthService::hash_password(const char* password) {
    if (!password || password[0] == '\0') {
        return common::Result<std::string>::error(common::ErrorCode::InvalidArgument);
    }

    // Generate random salt
    char salt_hex[kSaltLength + 1] = {};
    generate_random_hex(salt_hex, kSaltLength);

    std::vector<uint8_t> salt_bytes;
    if (!hex_to_bytes(salt_hex, kSaltLength, salt_bytes)) {
        return common::Result<std::string>::error(common::ErrorCode::Unknown);
    }

    uint8_t derived[32]{};
    if (!derive_pbkdf2_sha256(password, salt_bytes.data(), salt_bytes.size(), kPbkdf2Iterations,
                              derived)) {
        return common::Result<std::string>::error(common::ErrorCode::Unknown);
    }

    char hash_hex[kHashLength + 1] = {};
    bytes_to_hex(derived, sizeof(derived), hash_hex, sizeof(hash_hex));

    // Format: "pbkdf2$2000$salt_hex$hash_hex"
    std::string result =
        std::string(kPbkdf2Prefix) + std::to_string(kPbkdf2Iterations) + "$" + salt_hex + "$" + hash_hex;
    return common::Result<std::string>::ok(std::move(result));
}

bool AuthService::verify_password(const char* password, const char* stored_hash) {
    if (!password || !stored_hash) {
        return false;
    }

    if (std::strncmp(stored_hash, kPbkdf2Prefix, std::strlen(kPbkdf2Prefix)) == 0) {
        const char* iter_start = stored_hash + std::strlen(kPbkdf2Prefix);
        const char* iter_end = std::strchr(iter_start, '$');
        if (!iter_end) {
            return false;
        }

        char iter_buf[16] = {};
        const size_t iter_len = static_cast<size_t>(iter_end - iter_start);
        if (iter_len == 0 || iter_len >= sizeof(iter_buf)) {
            return false;
        }
        std::memcpy(iter_buf, iter_start, iter_len);
        char* parse_end = nullptr;
        const unsigned long iterations = std::strtoul(iter_buf, &parse_end, 10);
        if (!parse_end || *parse_end != '\0' || iterations == 0) {
            return false;
        }

        const char* salt_start = iter_end + 1;
        const char* salt_end = std::strchr(salt_start, '$');
        if (!salt_end) {
            return false;
        }

        const size_t salt_hex_len = static_cast<size_t>(salt_end - salt_start);
        if (salt_hex_len != kSaltLength) {
            return false;
        }

        std::vector<uint8_t> salt_bytes;
        if (!hex_to_bytes(salt_start, salt_hex_len, salt_bytes)) {
            return false;
        }

        const char* expected_hash = salt_end + 1;
        if (std::strlen(expected_hash) != kHashLength) {
            return false;
        }

        uint8_t derived[32]{};
        if (!derive_pbkdf2_sha256(password, salt_bytes.data(), salt_bytes.size(),
                                  static_cast<uint32_t>(iterations), derived)) {
            return false;
        }

        char computed_hash[kHashLength + 1] = {};
        bytes_to_hex(derived, sizeof(derived), computed_hash, sizeof(computed_hash));
        return secure_equals(computed_hash, expected_hash);
    }

    return verify_legacy_sha256_password(password, stored_hash);
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
        const size_t offset = i * 2U;
        std::snprintf(out + offset, hex_len + 1U - offset, "%02x", bytes[i]);
    }
    out[hex_len] = '\0';
}

std::string AuthService::sha256_hex(const uint8_t* data, size_t len) {
    uint8_t hash[32] = {};
    sha256_bytes(data, len, hash);

    char hex[65] = {};
    for (int i = 0; i < 32; ++i) {
        const size_t offset = static_cast<size_t>(i) * 2U;
        std::snprintf(hex + offset, sizeof(hex) - offset, "%02x", hash[i]);
    }
    return std::string(hex);
}

} // namespace auth_service
