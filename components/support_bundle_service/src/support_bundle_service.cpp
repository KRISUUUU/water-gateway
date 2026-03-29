#include "support_bundle_service/support_bundle_service.hpp"
#include "config_store/config_store.hpp"
#include "common/security_posture.hpp"
#include "diagnostics_service/diagnostics_service.hpp"
#include "health_monitor/health_monitor.hpp"
#include "meter_registry/meter_registry.hpp"
#include "metrics_service/metrics_service.hpp"
#include "ota_manager/ota_manager.hpp"
#include "persistent_log_buffer/persistent_log_buffer.hpp"

#include <memory>
#include <string>

#include "cJSON.h"

#ifndef HOST_TEST_BUILD
#include "esp_system.h"
#endif

namespace support_bundle_service {

namespace {

using JsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
using JsonStringPtr = std::unique_ptr<char, decltype(&cJSON_free)>;

JsonPtr make_object() {
    return JsonPtr(cJSON_CreateObject(), cJSON_Delete);
}

#ifndef HOST_TEST_BUILD
const char* reset_reason_str(unsigned int code) {
    switch (static_cast<esp_reset_reason_t>(code)) {
    case ESP_RST_UNKNOWN:
        return "unknown";
    case ESP_RST_POWERON:
        return "poweron";
    case ESP_RST_EXT:
        return "external";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "interrupt_watchdog";
    case ESP_RST_TASK_WDT:
        return "task_watchdog";
    case ESP_RST_WDT:
        return "other_watchdog";
    case ESP_RST_DEEPSLEEP:
        return "deepsleep";
    case ESP_RST_BROWNOUT:
        return "brownout";
#ifdef ESP_RST_SDIO
    case ESP_RST_SDIO:
        return "sdio";
#endif
    default:
        return "unknown";
    }
}
#endif

const char* log_severity_str(persistent_log_buffer::LogSeverity s) {
    using persistent_log_buffer::LogSeverity;
    switch (s) {
    case LogSeverity::Debug:
        return "debug";
    case LogSeverity::Info:
        return "info";
    case LogSeverity::Warning:
        return "warning";
    case LogSeverity::Error:
        return "error";
    }
    return "unknown";
}

const char* ota_state_str(ota_manager::OtaState s) {
    return ota_manager::ota_state_to_string(s);
}

const char* config_load_source_str(config_store::ConfigLoadSource s) {
    using config_store::ConfigLoadSource;
    switch (s) {
    case ConfigLoadSource::None:
        return "none";
    case ConfigLoadSource::PrimaryNvs:
        return "primary_nvs";
    case ConfigLoadSource::BackupNvs:
        return "backup_nvs";
    case ConfigLoadSource::Defaults:
        return "defaults";
    default:
        return "unknown";
    }
}

cJSON* build_redacted_config_json(const config_store::AppConfig& c) {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return nullptr;
    }

    cJSON_AddNumberToObject(root, "version", static_cast<double>(c.version));

    cJSON* device = cJSON_AddObjectToObject(root, "device");
    cJSON_AddStringToObject(device, "name", c.device.name);
    cJSON_AddStringToObject(device, "hostname", c.device.hostname);

    cJSON* wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddStringToObject(wifi, "ssid", c.wifi.ssid);
    cJSON_AddStringToObject(wifi, "password", config_store::kRedactedValue);
    cJSON_AddNumberToObject(wifi, "max_retries", static_cast<double>(c.wifi.max_retries));

    cJSON* mqtt = cJSON_AddObjectToObject(root, "mqtt");
    cJSON_AddBoolToObject(mqtt, "enabled", c.mqtt.enabled);
    cJSON_AddStringToObject(mqtt, "host", c.mqtt.host);
    cJSON_AddNumberToObject(mqtt, "port", static_cast<double>(c.mqtt.port));
    cJSON_AddStringToObject(mqtt, "username", config_store::kRedactedValue);
    cJSON_AddStringToObject(mqtt, "password", config_store::kRedactedValue);
    cJSON_AddStringToObject(mqtt, "prefix", c.mqtt.prefix);
    cJSON_AddStringToObject(mqtt, "client_id", c.mqtt.client_id);
    cJSON_AddNumberToObject(mqtt, "qos", static_cast<double>(c.mqtt.qos));
    cJSON_AddBoolToObject(mqtt, "use_tls", c.mqtt.use_tls);

    cJSON* radio = cJSON_AddObjectToObject(root, "radio");
    cJSON_AddNumberToObject(radio, "frequency_khz", static_cast<double>(c.radio.frequency_khz));
    cJSON_AddNumberToObject(radio, "data_rate", static_cast<double>(c.radio.data_rate));
    cJSON_AddBoolToObject(radio, "auto_recovery", c.radio.auto_recovery);

    cJSON* logging = cJSON_AddObjectToObject(root, "logging");
    cJSON_AddNumberToObject(logging, "level", static_cast<double>(c.logging.level));

    cJSON* auth = cJSON_AddObjectToObject(root, "auth");
    cJSON_AddStringToObject(auth, "admin_password_hash", config_store::kRedactedValue);
    cJSON_AddNumberToObject(auth, "session_timeout_s",
                            static_cast<double>(c.auth.session_timeout_s));

    return root;
}

cJSON* build_metrics_json(const metrics_service::RuntimeMetrics& m) {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return nullptr;
    }
    cJSON_AddNumberToObject(root, "uptime_s", static_cast<double>(m.uptime_s));
    cJSON_AddNumberToObject(root, "free_heap_bytes", static_cast<double>(m.free_heap_bytes));
    cJSON_AddNumberToObject(root, "min_free_heap_bytes",
                            static_cast<double>(m.min_free_heap_bytes));
    cJSON_AddNumberToObject(root, "largest_free_block", static_cast<double>(m.largest_free_block));
    cJSON_AddNumberToObject(root, "free_internal_heap_bytes",
                            static_cast<double>(m.free_internal_heap_bytes));
    cJSON_AddNumberToObject(root, "min_internal_heap_bytes",
                            static_cast<double>(m.min_internal_heap_bytes));
    cJSON_AddNumberToObject(root, "reset_reason_code", static_cast<double>(m.reset_reason_code));
#ifndef HOST_TEST_BUILD
    cJSON_AddStringToObject(root, "reset_reason", reset_reason_str(m.reset_reason_code));
#else
    cJSON_AddStringToObject(root, "reset_reason", "host_build");
#endif
    return root;
}

cJSON* build_health_json(const health_monitor::HealthSnapshot& h) {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return nullptr;
    }
    cJSON_AddStringToObject(root, "state", health_monitor::HealthMonitor::state_to_string(h.state));
    cJSON_AddNumberToObject(root, "warning_count", static_cast<double>(h.warning_count));
    cJSON_AddNumberToObject(root, "error_count", static_cast<double>(h.error_count));
    cJSON_AddNumberToObject(root, "uptime_s", static_cast<double>(h.uptime_s));
    cJSON_AddNumberToObject(root, "last_transition_uptime_s",
                            static_cast<double>(h.last_transition_uptime_s));
    cJSON_AddNumberToObject(root, "last_warning_uptime_s",
                            static_cast<double>(h.last_warning_uptime_s));
    cJSON_AddNumberToObject(root, "last_error_uptime_s",
                            static_cast<double>(h.last_error_uptime_s));
    cJSON_AddStringToObject(root, "last_warning_msg", h.last_warning_msg.c_str());
    cJSON_AddStringToObject(root, "last_error_msg", h.last_error_msg.c_str());
    return root;
}

cJSON* build_logs_json() {
    const auto lines = persistent_log_buffer::PersistentLogBuffer::instance().lines();
    cJSON* arr = cJSON_CreateArray();
    if (!arr) {
        return nullptr;
    }
    for (const auto& e : lines) {
        cJSON* item = cJSON_CreateObject();
        if (!item) {
            continue;
        }
        cJSON_AddNumberToObject(item, "timestamp_us", static_cast<double>(e.timestamp_us));
        cJSON_AddStringToObject(item, "severity", log_severity_str(e.severity));
        cJSON_AddStringToObject(item, "message", e.message.c_str());
        cJSON_AddItemToArray(arr, item);
    }
    return arr;
}

cJSON* build_meters_json() {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return nullptr;
    }
    const auto detected = meter_registry::MeterRegistry::instance().detected_meters();
    const auto watchlist = meter_registry::MeterRegistry::instance().watchlist();
    cJSON_AddNumberToObject(root, "detected_count", static_cast<double>(detected.size()));
    cJSON_AddNumberToObject(root, "watchlist_count", static_cast<double>(watchlist.size()));
    return root;
}

cJSON* build_ota_json() {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return nullptr;
    }
    const auto ota = ota_manager::OtaManager::instance().status();
    cJSON_AddStringToObject(root, "state", ota_state_str(ota.state));
    cJSON_AddNumberToObject(root, "progress_pct", static_cast<double>(ota.progress_pct));
    cJSON_AddStringToObject(root, "message", ota.message);
    cJSON_AddStringToObject(root, "current_version", ota.current_version);
    cJSON_AddBoolToObject(root, "boot_pending_verify", ota.boot_pending_verify);
    cJSON_AddBoolToObject(root, "boot_marked_valid", ota.boot_marked_valid);
    cJSON_AddNumberToObject(root, "boot_mark_attempts", static_cast<double>(ota.boot_mark_attempts));
    cJSON_AddNumberToObject(root, "boot_mark_failures", static_cast<double>(ota.boot_mark_failures));
    cJSON_AddNumberToObject(root, "last_boot_mark_error",
                            static_cast<double>(ota.last_boot_mark_error));
    cJSON_AddStringToObject(root, "boot_validation_note", ota.boot_validation_note);
    return root;
}

cJSON* build_security_posture_json() {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return nullptr;
    }
    const auto sec = common::build_security_posture();
    cJSON_AddBoolToObject(root, "secure_boot_enabled", sec.secure_boot_enabled);
    cJSON_AddBoolToObject(root, "flash_encryption_enabled", sec.flash_encryption_enabled);
    cJSON_AddBoolToObject(root, "nvs_encryption_enabled", sec.nvs_encryption_enabled);
    cJSON_AddBoolToObject(root, "anti_rollback_enabled", sec.anti_rollback_enabled);
    cJSON_AddBoolToObject(root, "ota_rollback_enabled", sec.ota_rollback_enabled);
    cJSON_AddBoolToObject(root, "production_hardening_ready",
                          common::build_is_hardened_for_production());
    cJSON* missing = cJSON_AddObjectToObject(root, "hardening_missing");
    cJSON_AddBoolToObject(missing, "secure_boot", !sec.secure_boot_enabled);
    cJSON_AddBoolToObject(missing, "flash_encryption", !sec.flash_encryption_enabled);
    cJSON_AddBoolToObject(missing, "nvs_encryption", !sec.nvs_encryption_enabled);
    cJSON_AddBoolToObject(missing, "anti_rollback", !sec.anti_rollback_enabled);
    cJSON_AddBoolToObject(missing, "ota_rollback", !sec.ota_rollback_enabled);
    const uint32_t missing_count = static_cast<uint32_t>((!sec.secure_boot_enabled ? 1U : 0U) +
                                                         (!sec.flash_encryption_enabled ? 1U : 0U) +
                                                         (!sec.nvs_encryption_enabled ? 1U : 0U) +
                                                         (!sec.anti_rollback_enabled ? 1U : 0U) +
                                                         (!sec.ota_rollback_enabled ? 1U : 0U));
    cJSON_AddNumberToObject(root, "hardening_missing_count", static_cast<double>(missing_count));
    return root;
}

cJSON* build_diagnostics_json(const diagnostics_service::DiagnosticsSnapshot& snap) {
    const std::string diagnostics_json = diagnostics_service::DiagnosticsService::to_json(snap);
    cJSON* parsed = cJSON_Parse(diagnostics_json.c_str());
    if (parsed) {
        return parsed;
    }
    cJSON* fallback = cJSON_CreateObject();
    if (fallback) {
        cJSON_AddStringToObject(fallback, "error", "diagnostics_parse_failed");
    }
    return fallback;
}

cJSON* build_config_or_error_json() {
    const auto status = config_store::ConfigStore::instance().runtime_status();
    if (!config_store::ConfigStore::instance().is_initialized() ||
        !config_store::ConfigStore::instance().is_loaded()) {
        cJSON* err = cJSON_CreateObject();
        if (err) {
            cJSON_AddStringToObject(err, "error", "config_not_loaded");
            cJSON* store = cJSON_AddObjectToObject(err, "store_status");
            if (store) {
                cJSON_AddStringToObject(store, "load_source",
                                        config_load_source_str(status.load_source));
                cJSON_AddStringToObject(store, "last_load_error",
                                        common::error_code_to_string(status.last_load_error));
            }
        }
        return err;
    }
    cJSON* cfg = build_redacted_config_json(config_store::ConfigStore::instance().config());
    if (!cfg) {
        return nullptr;
    }
    cJSON* store = cJSON_AddObjectToObject(cfg, "store_status");
    if (!store) {
        return cfg;
    }
    cJSON_AddStringToObject(store, "load_source", config_load_source_str(status.load_source));
    cJSON_AddBoolToObject(store, "used_defaults", status.used_defaults);
    cJSON_AddBoolToObject(store, "loaded_from_backup", status.loaded_from_backup);
    cJSON_AddBoolToObject(store, "defaults_persisted", status.defaults_persisted);
    cJSON_AddBoolToObject(store, "defaults_persist_deferred", status.defaults_persist_deferred);
    cJSON_AddStringToObject(store, "last_load_error",
                            common::error_code_to_string(status.last_load_error));
    cJSON_AddStringToObject(store, "last_persist_error",
                            common::error_code_to_string(status.last_persist_error));
    cJSON_AddStringToObject(store, "last_migration_error",
                            common::error_code_to_string(status.last_migration_error));
    cJSON_AddNumberToObject(store, "load_attempts", static_cast<double>(status.load_attempts));
    cJSON_AddNumberToObject(store, "load_failures", static_cast<double>(status.load_failures));
    cJSON_AddNumberToObject(store, "primary_read_failures",
                            static_cast<double>(status.primary_read_failures));
    cJSON_AddNumberToObject(store, "backup_read_failures",
                            static_cast<double>(status.backup_read_failures));
    cJSON_AddNumberToObject(store, "validation_failures",
                            static_cast<double>(status.validation_failures));
    cJSON_AddNumberToObject(store, "migration_attempts",
                            static_cast<double>(status.migration_attempts));
    cJSON_AddNumberToObject(store, "migration_failures",
                            static_cast<double>(status.migration_failures));
    cJSON_AddNumberToObject(store, "save_attempts", static_cast<double>(status.save_attempts));
    cJSON_AddNumberToObject(store, "save_successes", static_cast<double>(status.save_successes));
    cJSON_AddNumberToObject(store, "save_failures", static_cast<double>(status.save_failures));
    cJSON_AddNumberToObject(store, "save_validation_rejects",
                            static_cast<double>(status.save_validation_rejects));
    return cfg;
}

bool add_owned_item(cJSON* parent, const char* key, cJSON* item) {
    if (!parent || !key || !item) {
        return false;
    }
    cJSON_AddItemToObject(parent, key, item);
    return true;
}

} // namespace

SupportBundleService& SupportBundleService::instance() {
    static SupportBundleService service;
    return service;
}

common::Result<std::string> SupportBundleService::generate_bundle_json() const {
    auto diag_res = diagnostics_service::DiagnosticsService::instance().snapshot();
    if (diag_res.is_error()) {
        return common::Result<std::string>::error(diag_res.error());
    }

    auto metrics_res = metrics_service::MetricsService::instance().snapshot();
    if (metrics_res.is_error()) {
        return common::Result<std::string>::error(metrics_res.error());
    }

    auto health_res = health_monitor::HealthMonitor::instance().snapshot();
    if (health_res.is_error()) {
        return common::Result<std::string>::error(health_res.error());
    }

    JsonPtr root = make_object();
    if (!root) {
        return common::Result<std::string>::error(common::ErrorCode::Unknown);
    }

    cJSON_AddStringToObject(root.get(), "format", "water-gateway-support-bundle");
    cJSON_AddNumberToObject(root.get(), "format_version", 1);

    add_owned_item(root.get(), "diagnostics", build_diagnostics_json(diag_res.value()));
    add_owned_item(root.get(), "metrics", build_metrics_json(metrics_res.value()));
    add_owned_item(root.get(), "health", build_health_json(health_res.value()));
    add_owned_item(root.get(), "config", build_config_or_error_json());
    add_owned_item(root.get(), "logs", build_logs_json());
    add_owned_item(root.get(), "meters", build_meters_json());
    add_owned_item(root.get(), "ota", build_ota_json());
    add_owned_item(root.get(), "security_posture", build_security_posture_json());

    JsonStringPtr printed(cJSON_PrintUnformatted(root.get()), cJSON_free);
    if (!printed) {
        return common::Result<std::string>::error(common::ErrorCode::Unknown);
    }
    return common::Result<std::string>::ok(std::string(printed.get()));
}

} // namespace support_bundle_service
