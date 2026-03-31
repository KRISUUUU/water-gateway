#include "host_test_stubs.hpp"

#include "auth_service/auth_service.hpp"
#include "config_store/config_models.hpp"
#include "config_store/config_store.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {

void disable_mqtt_for_validation(config_store::AppConfig& cfg) {
    cfg.mqtt.enabled = false;
}

void clear_admin_hash(config_store::AppConfig& cfg) {
    std::memset(cfg.auth.admin_password_hash, 0, sizeof(cfg.auth.admin_password_hash));
}

void test_passwordless_login_allowed_in_provisioning() {
    auto cfg = config_store::ConfigStore::instance().config();
    disable_mqtt_for_validation(cfg);
    cfg.wifi.ssid[0] = '\0';
    clear_admin_hash(cfg);

    auto save = config_store::ConfigStore::instance().save(cfg);
    assert(save.is_ok());
    assert(save.value().valid);

    auto login = auth_service::AuthService::instance().login("bootstrap-pass");
    assert(login.is_ok());
    assert(login.value().valid);
    auth_service::AuthService::instance().logout();
    std::printf("  PASS: passwordless bootstrap login allowed in provisioning mode\n");
}

void test_passwordless_login_rejected_in_normal_mode() {
    auto cfg = config_store::ConfigStore::instance().config();
    disable_mqtt_for_validation(cfg);
    std::strncpy(cfg.wifi.ssid, "HomeWiFi", sizeof(cfg.wifi.ssid) - 1);
    cfg.wifi.ssid[sizeof(cfg.wifi.ssid) - 1] = '\0';
    clear_admin_hash(cfg);

    auto save = config_store::ConfigStore::instance().save(cfg);
    assert(save.is_ok());
    assert(save.value().valid);

    auto login = auth_service::AuthService::instance().login("should-fail");
    assert(login.is_error());
    assert(login.error() == common::ErrorCode::AuthFailed);
    std::printf("  PASS: passwordless login rejected in normal mode\n");
}

void test_password_hash_login_still_works() {
    auto cfg = config_store::ConfigStore::instance().config();
    disable_mqtt_for_validation(cfg);
    std::strncpy(cfg.wifi.ssid, "HomeWiFi", sizeof(cfg.wifi.ssid) - 1);
    cfg.wifi.ssid[sizeof(cfg.wifi.ssid) - 1] = '\0';

    auto hash = auth_service::AuthService::hash_password("CorrectHorse");
    assert(hash.is_ok());
    std::strncpy(cfg.auth.admin_password_hash, hash.value().c_str(),
                 sizeof(cfg.auth.admin_password_hash) - 1);
    cfg.auth.admin_password_hash[sizeof(cfg.auth.admin_password_hash) - 1] = '\0';

    auto save = config_store::ConfigStore::instance().save(cfg);
    assert(save.is_ok());
    assert(save.value().valid);

    auto wrong = auth_service::AuthService::instance().login("wrong");
    assert(wrong.is_error());
    assert(wrong.error() == common::ErrorCode::AuthFailed);

    auto ok = auth_service::AuthService::instance().login("CorrectHorse");
    assert(ok.is_ok());
    assert(ok.value().valid);
    auth_service::AuthService::instance().logout();
    std::printf("  PASS: hash-based login unchanged\n");
}

void test_login_rate_limit_enforced_after_repeated_failures() {
    auto cfg = config_store::ConfigStore::instance().config();
    disable_mqtt_for_validation(cfg);
    std::strncpy(cfg.wifi.ssid, "HomeWiFi", sizeof(cfg.wifi.ssid) - 1);
    cfg.wifi.ssid[sizeof(cfg.wifi.ssid) - 1] = '\0';

    auto hash = auth_service::AuthService::hash_password("RateLimitSecret");
    assert(hash.is_ok());
    std::strncpy(cfg.auth.admin_password_hash, hash.value().c_str(),
                 sizeof(cfg.auth.admin_password_hash) - 1);
    cfg.auth.admin_password_hash[sizeof(cfg.auth.admin_password_hash) - 1] = '\0';

    auto save = config_store::ConfigStore::instance().save(cfg);
    assert(save.is_ok());
    assert(save.value().valid);

    for (int i = 0; i < 5; ++i) {
        auto wrong = auth_service::AuthService::instance().login("wrong-password");
        assert(wrong.is_error());
        assert(wrong.error() == common::ErrorCode::AuthFailed);
    }

    auto blocked = auth_service::AuthService::instance().login("wrong-password");
    assert(blocked.is_error());
    assert(blocked.error() == common::ErrorCode::AuthRateLimited);
    const int32_t retry_after = auth_service::AuthService::instance().retry_after_seconds();
    assert(retry_after > 0);
    assert(retry_after <= 60);

    auto blocked_correct = auth_service::AuthService::instance().login("RateLimitSecret");
    assert(blocked_correct.is_error());
    assert(blocked_correct.error() == common::ErrorCode::AuthRateLimited);
    std::printf("  PASS: login rate limiting activates after repeated failures\n");
}

void test_session_timeout_updates_after_config_change() {
    constexpr uint32_t kClientId = 77;
    assert(auth_service::AuthService::instance().set_attempt_record_for_test(kClientId, 0, 0));

    auto cfg = config_store::ConfigStore::instance().config();
    disable_mqtt_for_validation(cfg);
    std::strncpy(cfg.wifi.ssid, "HomeWiFi", sizeof(cfg.wifi.ssid) - 1);
    cfg.wifi.ssid[sizeof(cfg.wifi.ssid) - 1] = '\0';
    cfg.auth.session_timeout_s = 120;

    auto hash = auth_service::AuthService::hash_password("TimeoutSecret");
    assert(hash.is_ok());
    std::strncpy(cfg.auth.admin_password_hash, hash.value().c_str(),
                 sizeof(cfg.auth.admin_password_hash) - 1);
    cfg.auth.admin_password_hash[sizeof(cfg.auth.admin_password_hash) - 1] = '\0';

    auto save = config_store::ConfigStore::instance().save(cfg);
    assert(save.is_ok());
    assert(save.value().valid);

    auto first = auth_service::AuthService::instance().login("TimeoutSecret", kClientId);
    assert(first.is_ok());
    const int64_t first_timeout = first.value().expires_epoch_s - first.value().created_epoch_s;
    assert(first_timeout == 120);
    auth_service::AuthService::instance().logout();

    cfg.auth.session_timeout_s = 240;
    save = config_store::ConfigStore::instance().save(cfg);
    assert(save.is_ok());
    assert(save.value().valid);

    auto second = auth_service::AuthService::instance().login("TimeoutSecret", kClientId);
    assert(second.is_ok());
    const int64_t second_timeout = second.value().expires_epoch_s - second.value().created_epoch_s;
    assert(second_timeout == 240);
    auth_service::AuthService::instance().logout();
    assert(auth_service::AuthService::instance().set_attempt_record_for_test(kClientId, 0, 0));
    std::printf("  PASS: session timeout updates after config change without reboot\n");
}

} // namespace

int main() {
    std::printf("=== test_auth_login_policy ===\n");

    auto cfg_init = config_store::ConfigStore::instance().initialize();
    assert(cfg_init.is_ok() || cfg_init.error() == common::ErrorCode::AlreadyInitialized);

    auto auth_init = auth_service::AuthService::instance().initialize();
    assert(auth_init.is_ok() || auth_init.error() == common::ErrorCode::AlreadyInitialized);

    test_passwordless_login_allowed_in_provisioning();
    test_passwordless_login_rejected_in_normal_mode();
    test_password_hash_login_still_works();
    test_login_rate_limit_enforced_after_repeated_failures();
    test_session_timeout_updates_after_config_change();

    std::printf("All auth login policy tests passed.\n");
    return 0;
}
