#pragma once

#include "common/result.hpp"
#include <cstdint>
#include <mutex>
#include <string>

namespace auth_service {

static constexpr size_t kTokenLength = 64; // Hex-encoded 32 bytes
static constexpr size_t kSaltLength = 64;  // Hex-encoded 32 bytes
static constexpr size_t kHashLength = 64;  // Hex-encoded SHA-256
static constexpr size_t kMaxPasswordLength = 64;

struct SessionInfo {
    char token[kTokenLength + 1];
    int64_t created_epoch_s;
    int64_t expires_epoch_s;
    bool valid;
};

struct LoginAttemptRecord {
    uint32_t client_id = 0;
    uint32_t fail_count = 0;
    int64_t last_fail_s = 0;
};

class AuthService {
  public:
    static constexpr size_t kMaxTrackedClients = 8;

    static AuthService& instance();

    common::Result<void> initialize();

    // Attempt login with plaintext password.
    // On success, returns a session token. On failure, returns error.
    common::Result<SessionInfo> login(const char* password, uint32_t client_id = 0);

    // Invalidate current session.
    common::Result<void> logout();

    // Check if a token is valid (not expired, matches active session).
    bool validate_session(const char* token);

    // Hash a password with a new random salt. Returns
    // "pbkdf2$<iterations>$<salt_hex>$<hash_hex>".
    static common::Result<std::string> hash_password(const char* password);

    // Verify a plaintext password against either the PBKDF2 format above or
    // the legacy "salt_hex:sha256_hex" format for backward compatibility.
    static bool verify_password(const char* password, const char* stored_hash);

    bool has_active_session() const {
        return session_.valid;
    }
    int32_t retry_after_seconds(uint32_t client_id = 0) const;

#ifdef HOST_TEST_BUILD
    bool set_attempt_record_for_test(uint32_t client_id, uint32_t fail_count, int64_t last_fail_s);
    bool has_attempt_record_for_test(uint32_t client_id) const;
#endif

  private:
    AuthService() = default;

    static void generate_random_hex(char* out, size_t hex_len);
    static std::string sha256_hex(const uint8_t* data, size_t len);

    bool initialized_ = false;
    SessionInfo session_{};
    uint32_t session_timeout_s_ = 3600;
    LoginAttemptRecord records_[kMaxTrackedClients]{};
    mutable std::mutex mutex_;
};

} // namespace auth_service
