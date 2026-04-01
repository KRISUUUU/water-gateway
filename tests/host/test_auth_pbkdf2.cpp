#include "auth_service/auth_service.hpp"
#include "persistent_log_buffer/persistent_log_buffer.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using auth_service::AuthService;
using persistent_log_buffer::LogSeverity;
using persistent_log_buffer::PersistentLogBuffer;

namespace {
std::string legacy_host_sha256_hex(const uint8_t* data, size_t len) {
    uint32_t h = 0x811c9dc5;
    for (size_t i = 0; i < len; ++i) {
        h ^= data[i];
        h *= 0x01000193;
    }

    uint8_t hash[32]{};
    for (int i = 0; i < 32; ++i) {
        hash[i] = static_cast<uint8_t>((h >> ((i % 4) * 8)) ^ (i * 37));
        h = h * 1103515245 + 12345;
    }

    char hex[65]{};
    for (int i = 0; i < 32; ++i) {
        std::snprintf(hex + i * 2, sizeof(hex) - static_cast<size_t>(i) * 2, "%02x", hash[i]);
    }
    return std::string(hex);
}

std::string make_legacy_hash(const char* password) {
    const char* salt_hex = "00112233445566778899aabbccddeeff";
    std::vector<uint8_t> input = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
    };
    input.insert(input.end(), reinterpret_cast<const uint8_t*>(password),
                 reinterpret_cast<const uint8_t*>(password) + std::strlen(password));
    return std::string(salt_hex) + ":" + legacy_host_sha256_hex(input.data(), input.size());
}
} // namespace

static void test_hash_password_uses_pbkdf2_format() {
    auto result = AuthService::hash_password("hello123");
    assert(result.is_ok());
    assert(result.value().rfind("pbkdf2$2000$", 0) == 0);
    printf("  PASS: pbkdf2 format prefix\n");
}

static void test_verify_new_format() {
    auto hash = AuthService::hash_password("secret123");
    assert(hash.is_ok());
    assert(AuthService::verify_password("secret123", hash.value().c_str()));
    assert(!AuthService::verify_password("wrong", hash.value().c_str()));
    printf("  PASS: verify new pbkdf2 hash\n");
}

static void test_verify_legacy_format() {
    const std::string legacy = make_legacy_hash("legacy-pass");
    assert(AuthService::verify_password("legacy-pass", legacy.c_str()));
    assert(!AuthService::verify_password("wrong", legacy.c_str()));
    printf("  PASS: verify legacy salted SHA-256 hash\n");
}

static void test_persistent_log_buffer_append_and_lines() {
    auto& buffer = PersistentLogBuffer::instance();
    auto before = buffer.lines().size();
    auto append = buffer.append(LogSeverity::Warning, "unit-test-line");
    assert(append.is_ok());
    auto lines = buffer.lines();
    assert(lines.size() == before + 1);
    assert(std::strcmp(lines.back().message, "unit-test-line") == 0);
    assert(lines.back().severity == LogSeverity::Warning);
    printf("  PASS: persistent log append/lines\n");
}

static void test_persistent_log_buffer_evicts_oldest() {
    auto& buffer = PersistentLogBuffer::instance();
    for (size_t i = 0; i < PersistentLogBuffer::kMaxLines + 5; ++i) {
        char msg[32]{};
        std::snprintf(msg, sizeof(msg), "log-%03zu", i);
        auto append = buffer.append(LogSeverity::Info, msg);
        assert(append.is_ok());
    }

    auto lines = buffer.lines();
    assert(lines.size() == PersistentLogBuffer::kMaxLines);
    assert(std::strcmp(lines.front().message, "log-000") != 0);
    assert(std::strcmp(lines.back().message, "log-204") == 0);
    printf("  PASS: persistent log evicts oldest\n");
}

static void test_persistent_log_buffer_truncates_and_null_terminates() {
    auto& buffer = PersistentLogBuffer::instance();
    std::string long_line(PersistentLogBuffer::kMaxMessageChars + 32, 'x');
    auto append = buffer.append(LogSeverity::Error, long_line.c_str());
    assert(append.is_ok());

    auto lines = buffer.lines();
    assert(!lines.empty());
    const auto& last = lines.back();
    assert(last.message[PersistentLogBuffer::kMaxMessageChars - 1] == '\0');
    assert(std::strlen(last.message) == PersistentLogBuffer::kMaxMessageChars - 1);
    printf("  PASS: persistent log truncates long entries\n");
}

int main() {
    printf("=== test_auth_pbkdf2 ===\n");
    test_hash_password_uses_pbkdf2_format();
    test_verify_new_format();
    test_verify_legacy_format();
    test_persistent_log_buffer_append_and_lines();
    test_persistent_log_buffer_evicts_oldest();
    test_persistent_log_buffer_truncates_and_null_terminates();
    printf("All PBKDF2/log buffer tests passed.\n");
    return 0;
}
