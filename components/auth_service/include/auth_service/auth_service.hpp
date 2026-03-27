#pragma once

#include "common/result.hpp"
#include <cstdint>
#include <mutex>
#include <string>

namespace auth_service {

static constexpr size_t kTokenLength = 64; // Hex-encoded 32 bytes
static constexpr size_t kSaltLength = 32;  // Hex-encoded 16 bytes (legacy SHA-256)
static constexpr size_t kHashLength = 64;  // Hex-encoded SHA-256 / PBKDF2 dk
static constexpr size_t kMaxPasswordLength = 64;

// PBKDF2-HMAC-SHA256 parameters (new password hashes only)
static constexpr unsigned int kPbkdf2Iterations = 10000;
static constexpr size_t kPbkdf2SaltBytes = 16;  // 16 raw bytes → 32 hex chars
static constexpr size_t kPbkdf2DkBytes = 32;    // 32 raw bytes → 64 hex chars

struct SessionInfo {
    char token[kTokenLength + 1];
    int64_t created_epoch_s;
    int64_t expires_epoch_s;
    bool valid;
};

class AuthService {
  public:
    static AuthService& instance();

    common::Result<void> initialize();

    // Attempt login with plaintext password.
    // On success, returns a session token. On failure, returns error.
    common::Result<SessionInfo> login(const char* password);

    // Invalidate current session.
    common::Result<void> logout();

    // Check if a token is valid (not expired, matches active session).
    bool validate_session(const char* token);

    // Hash a password with a new random salt. Returns "salt:hash" format.
    // Used during provisioning to set initial password.
    static common::Result<std::string> hash_password(const char* password);

    // Verify a plaintext password against a "salt:hash" stored hash.
    static bool verify_password(const char* password, const char* stored_hash);

    bool has_active_session() const {
        return session_.valid;
    }
    int32_t retry_after_seconds() const;

  private:
    AuthService() = default;

    static void generate_random_hex(char* out, size_t hex_len);
    static std::string sha256_hex(const uint8_t* data, size_t len);

    // PBKDF2-HMAC-SHA256: derives kPbkdf2DkBytes into out[].
    // Returns false on error (e.g. MBedTLS unavailable in stubs).
    static bool pbkdf2_sha256(const uint8_t* password, size_t pwd_len,
                              const uint8_t* salt, size_t salt_len,
                              uint8_t* out);

    // Returns true if stored_hash starts with the PBKDF2 prefix.
    static bool is_pbkdf2_hash(const char* stored_hash);

    // Verify using legacy SHA-256 format: "salt_hex:hash_hex"
    static bool verify_sha256(const char* password, const char* stored_hash);

    // Verify using PBKDF2 format: "pbkdf2$iter$salt_hex$dk_hex"
    static bool verify_pbkdf2(const char* password, const char* stored_hash);

    bool initialized_ = false;
    SessionInfo session_{};
    uint32_t session_timeout_s_ = 3600;
    uint32_t failed_login_count_ = 0;
    int64_t last_failed_login_s_ = 0;
    mutable std::mutex mutex_;
};

} // namespace auth_service
