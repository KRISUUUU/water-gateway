// Host tests for radio scheduler configuration and RadioProfileManager.
//
// Covers:
//   - RadioSchedulerMode enum values and string labels
//   - RadioProfileMask constants
//   - Config v2 → v3 migration applies scheduler defaults
//   - Config v4 → v5 migration applies PRIOS discovery defaults
//   - validate_config passes with default config (v3)
//   - validate_config fails with empty enabled_profiles
//   - validate_config fails with unknown scheduler_mode
//   - RadioProfileManager: configure, status, active_profile_id
//   - RadioProfileManager: Locked mode advance is a no-op
//   - RadioProfileManager: Scan mode advance cycles through enabled profiles
//   - RadioProfileManager: single-profile scan stays put
//   - RadioProfileManager: wake source counters

#include "host_test_stubs.hpp"
#include "config_store/config_migration.hpp"
#include "config_store/config_models.hpp"
#include "config_store/config_validation.hpp"
#include "protocol_driver/protocol_ids.hpp"
#include "protocol_driver/radio_profile_manager.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace protocol_driver;
using namespace config_store;

namespace {

// ---- RadioSchedulerMode ----

void test_scheduler_mode_known_values() {
    assert(static_cast<uint8_t>(RadioSchedulerMode::Locked)   == 0);
    assert(static_cast<uint8_t>(RadioSchedulerMode::Priority) == 1);
    assert(static_cast<uint8_t>(RadioSchedulerMode::Scan)     == 2);
    std::printf("  PASS: RadioSchedulerMode known values\n");
}

void test_scheduler_mode_to_string() {
    assert(std::strcmp(radio_scheduler_mode_to_string(RadioSchedulerMode::Locked),   "Locked")   == 0);
    assert(std::strcmp(radio_scheduler_mode_to_string(RadioSchedulerMode::Priority), "Priority") == 0);
    assert(std::strcmp(radio_scheduler_mode_to_string(RadioSchedulerMode::Scan),     "Scan")     == 0);
    std::printf("  PASS: radio_scheduler_mode_to_string\n");
}

// ---- RadioProfileMask constants ----

void test_profile_mask_constants() {
    assert(kRadioProfileMaskNone         == 0x00);
    assert(kRadioProfileMaskWMbusT868    == 0x02); // 1 << 1
    assert(kRadioProfileMaskWMbusPriosR3 == 0x04); // 1 << 2
    assert(kRadioProfileMaskWMbusPriosR4 == 0x08); // 1 << 3
    // T-mode bit corresponds to WMbusT868 enum value 1
    assert(kRadioProfileMaskWMbusT868 ==
           (1U << static_cast<uint8_t>(RadioProfileId::WMbusT868)));
    assert(kRadioProfileMaskWMbusPriosR3 ==
           (1U << static_cast<uint8_t>(RadioProfileId::WMbusPriosR3)));
    std::printf("  PASS: RadioProfileMask constants\n");
}

// ---- Config migration v2 → v3 ----

void test_migrate_v2_to_v3_scheduler_defaults() {
    AppConfig cfg = AppConfig::make_default();
    cfg.version = 2;
    // Simulate a pre-v3 config without scheduler fields set
    cfg.radio.scheduler_mode   = static_cast<RadioSchedulerMode>(0xFF); // garbage
    cfg.radio.enabled_profiles = 0x00;                                  // empty

    auto result = migrate_to_current(cfg);
    assert(result.is_ok());
    assert(result.value().version == kCurrentConfigVersion);
    assert(result.value().radio.scheduler_mode   == RadioSchedulerMode::Locked);
    assert(result.value().radio.enabled_profiles == kRadioProfileMaskWMbusT868);
    std::printf("  PASS: v2→v3 migration applies scheduler defaults\n");
}

void test_migrate_v0_reaches_current() {
    AppConfig cfg{};
    cfg.version = 0;
    std::strncpy(cfg.device.name, "GW", sizeof(cfg.device.name) - 1);
    std::strncpy(cfg.device.hostname, "gw", sizeof(cfg.device.hostname) - 1);

    auto result = migrate_to_current(cfg);
    assert(result.is_ok());
    assert(result.value().version == kCurrentConfigVersion);
    assert(result.value().radio.scheduler_mode    == RadioSchedulerMode::Locked);
    assert(result.value().radio.enabled_profiles  == kRadioProfileMaskWMbusT868);
    // v4 fields must default to false on migration from old configs.
    assert(result.value().radio.prios_capture_campaign  == false);
    assert(result.value().radio.prios_discovery_mode    == false);
    assert(result.value().radio.prios_manchester_enabled == false);
    std::printf("  PASS: v0 chain migration reaches current version with scheduler+campaign defaults\n");
}

void test_migrate_v3_to_v4_campaign_defaults() {
    AppConfig cfg = AppConfig::make_default();
    cfg.version = 3;
    // Simulate a v3 config that doesn't have v4 fields yet.
    cfg.radio.prios_capture_campaign   = true;  // garbage — should be overwritten
    cfg.radio.prios_manchester_enabled = true;  // garbage — should be overwritten

    auto result = migrate_to_current(cfg);
    assert(result.is_ok());
    assert(result.value().version == kCurrentConfigVersion);
    assert(result.value().radio.prios_capture_campaign  == false);
    assert(result.value().radio.prios_discovery_mode    == false);
    assert(result.value().radio.prios_manchester_enabled == false);
    std::printf("  PASS: v3→v4 migration resets campaign fields to false\n");
}

void test_migrate_v4_to_v5_discovery_defaults() {
    AppConfig cfg = AppConfig::make_default();
    cfg.version = 4;
    cfg.radio.prios_capture_campaign = true;
    cfg.radio.prios_discovery_mode = true; // garbage; should be overwritten
    cfg.radio.prios_manchester_enabled = true;

    auto result = migrate_to_current(cfg);
    assert(result.is_ok());
    assert(result.value().version == kCurrentConfigVersion);
    assert(result.value().radio.prios_capture_campaign == true);
    assert(result.value().radio.prios_discovery_mode == false);
    assert(result.value().radio.prios_manchester_enabled == true);
    std::printf("  PASS: v4→v5 migration adds discovery field with safe default\n");
}

// ---- Validation ----

void test_default_config_validates() {
    const AppConfig cfg = AppConfig::make_default();
    const auto result = validate_config(cfg);
    // Default config may produce other errors (e.g. empty MQTT host) but must
    // not produce scheduler-related errors.
    bool found_radio_sched_error = false;
    for (const auto& issue : result.issues) {
        if (issue.severity == config_store::ValidationSeverity::Error &&
            (issue.field == "radio.scheduler_mode" ||
             issue.field == "radio.enabled_profiles")) {
            found_radio_sched_error = true;
        }
    }
    assert(!found_radio_sched_error);
    std::printf("  PASS: default config scheduler fields validate without errors\n");
}

void test_empty_enabled_profiles_fails_validation() {
    AppConfig cfg = AppConfig::make_default();
    cfg.radio.enabled_profiles = kRadioProfileMaskNone;
    const auto result = validate_config(cfg);
    assert(!result.valid);
    bool found = false;
    for (const auto& issue : result.issues) {
        if (issue.field == "radio.enabled_profiles" &&
            issue.severity == config_store::ValidationSeverity::Error) {
            found = true;
        }
    }
    assert(found);
    std::printf("  PASS: empty enabled_profiles fails validation\n");
}

void test_unknown_scheduler_mode_fails_validation() {
    AppConfig cfg = AppConfig::make_default();
    cfg.radio.scheduler_mode = static_cast<RadioSchedulerMode>(99);
    const auto result = validate_config(cfg);
    assert(!result.valid);
    bool found = false;
    for (const auto& issue : result.issues) {
        if (issue.field == "radio.scheduler_mode" &&
            issue.severity == config_store::ValidationSeverity::Error) {
            found = true;
        }
    }
    assert(found);
    std::printf("  PASS: unknown scheduler_mode fails validation\n");
}

// ---- RadioProfileManager ----

// Each test calls configure() to reset the singleton state.

void test_manager_configure_sets_active_profile() {
    auto& mgr = RadioProfileManager::instance();
    mgr.configure(RadioSchedulerMode::Locked, kRadioProfileMaskWMbusT868);
    const auto st = mgr.status();
    assert(st.active_profile_id  == RadioProfileId::WMbusT868);
    assert(st.scheduler_mode     == RadioSchedulerMode::Locked);
    assert(st.enabled_profiles   == kRadioProfileMaskWMbusT868);
    assert(st.profile_switch_count == 0);
    std::printf("  PASS: configure sets active profile to first enabled\n");
}

void test_manager_locked_advance_is_noop() {
    auto& mgr = RadioProfileManager::instance();
    mgr.configure(RadioSchedulerMode::Locked,
                  kRadioProfileMaskWMbusT868 | kRadioProfileMaskWMbusPriosR3);
    const RadioProfileId before = mgr.active_profile_id();
    mgr.advance();
    assert(mgr.active_profile_id() == before);
    assert(mgr.status().profile_switch_count == 0);
    std::printf("  PASS: Locked mode advance is a no-op\n");
}

void test_manager_scan_advance_cycles_profiles() {
    auto& mgr = RadioProfileManager::instance();
    mgr.configure(RadioSchedulerMode::Scan,
                  kRadioProfileMaskWMbusT868 | kRadioProfileMaskWMbusPriosR3);
    // Should start on T868
    assert(mgr.active_profile_id() == RadioProfileId::WMbusT868);
    // Advance → PriosR3
    RadioProfileId next = mgr.advance();
    assert(next == RadioProfileId::WMbusPriosR3);
    assert(mgr.status().profile_switch_count == 1);
    // Advance again → back to T868 (wrap around)
    next = mgr.advance();
    assert(next == RadioProfileId::WMbusT868);
    assert(mgr.status().profile_switch_count == 2);
    std::printf("  PASS: Scan mode advance cycles through enabled profiles\n");
}

void test_manager_scan_single_profile_stays_put() {
    auto& mgr = RadioProfileManager::instance();
    mgr.configure(RadioSchedulerMode::Scan, kRadioProfileMaskWMbusT868);
    const RadioProfileId before = mgr.active_profile_id();
    mgr.advance();
    // Only one enabled — should stay on T868
    assert(mgr.active_profile_id() == before);
    assert(mgr.status().profile_switch_count == 0);
    std::printf("  PASS: Scan with single enabled profile stays put\n");
}

void test_manager_wake_counters() {
    auto& mgr = RadioProfileManager::instance();
    mgr.configure(RadioSchedulerMode::Locked, kRadioProfileMaskWMbusT868);
    mgr.record_irq_wake();
    mgr.record_irq_wake();
    mgr.record_fallback_wake();
    const auto st = mgr.status();
    assert(st.irq_wake_count     == 2);
    assert(st.fallback_wake_count == 1);
    std::printf("  PASS: wake source counters tracked correctly\n");
}

void test_manager_configure_resets_counters() {
    auto& mgr = RadioProfileManager::instance();
    mgr.configure(RadioSchedulerMode::Locked, kRadioProfileMaskWMbusT868);
    mgr.record_irq_wake();
    mgr.record_fallback_wake();
    // Re-configure — counters reset to zero
    mgr.configure(RadioSchedulerMode::Scan, kRadioProfileMaskWMbusT868);
    const auto st = mgr.status();
    assert(st.irq_wake_count      == 0);
    assert(st.fallback_wake_count == 0);
    assert(st.profile_switch_count == 0);
    std::printf("  PASS: configure() resets all counters\n");
}

} // namespace

int main() {
    std::printf("=== test_radio_scheduler_config ===\n");

    test_scheduler_mode_known_values();
    test_scheduler_mode_to_string();
    test_profile_mask_constants();
    test_migrate_v2_to_v3_scheduler_defaults();
    test_migrate_v0_reaches_current();
    test_migrate_v3_to_v4_campaign_defaults();
    test_migrate_v4_to_v5_discovery_defaults();
    test_default_config_validates();
    test_empty_enabled_profiles_fails_validation();
    test_unknown_scheduler_mode_fails_validation();
    test_manager_configure_sets_active_profile();
    test_manager_locked_advance_is_noop();
    test_manager_scan_advance_cycles_profiles();
    test_manager_scan_single_profile_stays_put();
    test_manager_wake_counters();
    test_manager_configure_resets_counters();

    std::printf("All radio scheduler config tests passed.\n");
    return 0;
}
