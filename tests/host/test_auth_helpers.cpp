#include "host_test_stubs.hpp"
#include "config_store/config_models.hpp"
#include "config_store/config_store.hpp"
#include "auth_service/auth_service.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace auth_service;

static void test_hash_password() {
    auto result = AuthService::hash_password("mypassword");
    assert(result.is_ok());
    std::string hash = result.value();
    // Should be "salt:hash" format (32 + 1 + 64 = 97 chars)
    assert(hash.length() == 97);
    assert(hash[32] == ':');
    printf("  PASS: hash_password produces correct format\n");
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
    test_verify_correct_password();
    test_verify_wrong_password();
    test_hash_empty_password_fails();
    test_hash_null_password_fails();
    test_verify_null_stored_hash();
    test_verify_invalid_format();
    test_different_passwords_different_hashes();
    printf("All auth helper tests passed.\n");
    return 0;
}
