#include "host_test_stubs.hpp"

#include "auth_service/auth_service.hpp"
#include "config_store/config_models.hpp"
#include "config_store/config_store.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {

void prepare_password_config(const char* password) {
    auto cfg = config_store::ConfigStore::instance().config();
    std::strncpy(cfg.wifi.ssid, "HomeWiFi", sizeof(cfg.wifi.ssid) - 1);
    cfg.wifi.ssid[sizeof(cfg.wifi.ssid) - 1] = '\0';
    cfg.mqtt.enabled = false;
    auto hash = auth_service::AuthService::hash_password(password);
    assert(hash.is_ok());
    std::strncpy(cfg.auth.admin_password_hash, hash.value().c_str(),
                 sizeof(cfg.auth.admin_password_hash) - 1);
    cfg.auth.admin_password_hash[sizeof(cfg.auth.admin_password_hash) - 1] = '\0';
    auto save = config_store::ConfigStore::instance().save(cfg);
    assert(save.is_ok());
    assert(save.value().valid);
}

void clear_attempt_records() {
    for (uint32_t client_id = 0;
         client_id <= auth_service::AuthService::kMaxTrackedClients + 2; ++client_id) {
        assert(auth_service::AuthService::instance().set_attempt_record_for_test(client_id, 0, 0));
    }
    auth_service::AuthService::instance().logout();
}

void test_rate_limit_is_scoped_to_single_client() {
    clear_attempt_records();
    prepare_password_config("RateLimitSecret");

    for (int i = 0; i < 5; ++i) {
        auto wrong = auth_service::AuthService::instance().login("wrong-password", 1);
        assert(wrong.is_error());
        assert(wrong.error() == common::ErrorCode::AuthFailed);
    }

    auto blocked = auth_service::AuthService::instance().login("wrong-password", 1);
    assert(blocked.is_error());
    assert(blocked.error() == common::ErrorCode::AuthRateLimited);

    auto other_client = auth_service::AuthService::instance().login("wrong-password", 2);
    assert(other_client.is_error());
    assert(other_client.error() == common::ErrorCode::AuthFailed);
    std::printf("  PASS: rate limit is tracked per client\n");
}

void test_rate_limit_expires_after_window() {
    clear_attempt_records();
    prepare_password_config("RateLimitSecret");

    const int64_t now_s = static_cast<int64_t>(std::time(nullptr));
    assert(auth_service::AuthService::instance().set_attempt_record_for_test(1, 5, now_s - 61));

    auto retry = auth_service::AuthService::instance().login("wrong-password", 1);
    assert(retry.is_error());
    assert(retry.error() == common::ErrorCode::AuthFailed);
    std::printf("  PASS: expired lockout allows new attempts\n");
}

void test_oldest_record_is_reused_when_table_is_full() {
    clear_attempt_records();
    prepare_password_config("RateLimitSecret");

    for (uint32_t client_id = 1; client_id <= auth_service::AuthService::kMaxTrackedClients;
         ++client_id) {
        auto wrong = auth_service::AuthService::instance().login("wrong-password", client_id);
        assert(wrong.is_error());
        assert(wrong.error() == common::ErrorCode::AuthFailed);
        assert(auth_service::AuthService::instance().has_attempt_record_for_test(client_id));
    }

    const uint32_t newcomer = auth_service::AuthService::kMaxTrackedClients + 1;
    auto wrong = auth_service::AuthService::instance().login("wrong-password", newcomer);
    assert(wrong.is_error());
    assert(wrong.error() == common::ErrorCode::AuthFailed);
    assert(!auth_service::AuthService::instance().has_attempt_record_for_test(1));
    assert(auth_service::AuthService::instance().has_attempt_record_for_test(newcomer));
    std::printf("  PASS: oldest client record is evicted when tracking table is full\n");
}

void test_global_fallback_client_id_zero_is_unchanged() {
    clear_attempt_records();
    prepare_password_config("RateLimitSecret");

    for (int i = 0; i < 5; ++i) {
        auto wrong = auth_service::AuthService::instance().login("wrong-password");
        assert(wrong.is_error());
        assert(wrong.error() == common::ErrorCode::AuthFailed);
    }
    auto blocked = auth_service::AuthService::instance().login("wrong-password");
    assert(blocked.is_error());
    assert(blocked.error() == common::ErrorCode::AuthRateLimited);
    assert(auth_service::AuthService::instance().retry_after_seconds() > 0);
    std::printf("  PASS: client_id=0 fallback keeps legacy global behavior\n");
}

} // namespace

int main() {
    std::printf("=== test_auth_rate_limit ===\n");

    auto cfg_init = config_store::ConfigStore::instance().initialize();
    assert(cfg_init.is_ok() || cfg_init.error() == common::ErrorCode::AlreadyInitialized);
    auto auth_init = auth_service::AuthService::instance().initialize();
    assert(auth_init.is_ok() || auth_init.error() == common::ErrorCode::AlreadyInitialized);

    test_rate_limit_is_scoped_to_single_client();
    test_rate_limit_expires_after_window();
    test_oldest_record_is_reused_when_table_is_full();
    test_global_fallback_client_id_zero_is_unchanged();

    std::printf("All auth rate limit tests passed.\n");
    return 0;
}
