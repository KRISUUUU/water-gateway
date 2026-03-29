#include "host_test_stubs.hpp"
#include "config_store/config_store.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace config_store;

static void test_initialize_reports_default_fallback_status() {
    auto init = ConfigStore::instance().initialize();
    assert(init.is_ok());

    const ConfigRuntimeStatus st = ConfigStore::instance().runtime_status();
    assert(st.initialize_count == 1);
    assert(st.load_attempts == 1);
    assert(st.load_failures == 1);
    assert(st.used_defaults);
    assert(st.load_source == ConfigLoadSource::Defaults);
    assert(st.last_load_error == common::ErrorCode::NvsReadFailed);
    assert(st.defaults_persisted);
    assert(!st.defaults_persist_deferred);
    std::printf("  PASS: initialize fallback status is visible\n");
}

static void test_save_status_counters() {
    auto cfg = ConfigStore::instance().config();
    cfg.mqtt.enabled = false;
    std::strncpy(cfg.device.name, "GW-Host", sizeof(cfg.device.name) - 1);
    cfg.device.name[sizeof(cfg.device.name) - 1] = '\0';
    std::strncpy(cfg.device.hostname, "gw-host", sizeof(cfg.device.hostname) - 1);
    cfg.device.hostname[sizeof(cfg.device.hostname) - 1] = '\0';

    auto ok_save = ConfigStore::instance().save(cfg);
    assert(ok_save.is_ok());
    assert(ok_save.value().valid);

    const ConfigRuntimeStatus st_after_ok = ConfigStore::instance().runtime_status();
    assert(st_after_ok.save_attempts >= 1);
    assert(st_after_ok.save_successes >= 1);

    cfg.device.name[0] = '\0';
    auto bad_save = ConfigStore::instance().save(cfg);
    assert(bad_save.is_ok());
    assert(!bad_save.value().valid);

    const ConfigRuntimeStatus st_after_bad = ConfigStore::instance().runtime_status();
    assert(st_after_bad.save_attempts >= 2);
    assert(st_after_bad.save_validation_rejects >= 1);
    std::printf("  PASS: save success/reject counters are visible\n");
}

static void test_save_normalizes_non_terminated_strings() {
    auto cfg = ConfigStore::instance().config();
    cfg.mqtt.enabled = false;
    std::memset(cfg.device.name, 'A', sizeof(cfg.device.name));
    std::memset(cfg.device.hostname, 'b', sizeof(cfg.device.hostname));
    cfg.device.hostname[0] = 'g';
    cfg.device.hostname[1] = 'w';

    auto save = ConfigStore::instance().save(cfg);
    assert(save.is_ok());
    assert(save.value().valid);

    auto loaded = ConfigStore::instance().config();
    assert(loaded.device.name[sizeof(loaded.device.name) - 1] == '\0');
    assert(loaded.device.hostname[sizeof(loaded.device.hostname) - 1] == '\0');
    std::printf("  PASS: string normalization prevents unterminated config strings\n");
}

int main() {
    std::printf("=== test_config_store_status ===\n");
    test_initialize_reports_default_fallback_status();
    test_save_status_counters();
    test_save_normalizes_non_terminated_strings();
    std::printf("All config store status tests passed.\n");
    return 0;
}
