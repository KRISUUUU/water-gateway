#include "support_bundle_service/support_bundle_service.hpp"
#include "config_store/config_store.hpp"
#include "diagnostics_service/diagnostics_service.hpp"
#include "health_monitor/health_monitor.hpp"
#include "meter_registry/meter_registry.hpp"
#include "metrics_service/metrics_service.hpp"
#include "ota_manager/ota_manager.hpp"
#include "persistent_log_buffer/persistent_log_buffer.hpp"

#include <memory>
#include <string>

#include "cJSON.h"
#include <ctime>

namespace support_bundle_service {

namespace {

using JsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
using JsonStringPtr = std::unique_ptr<char, decltype(&cJSON_free)>;

JsonPtr make_object() {
    return JsonPtr(cJSON_CreateObject(), cJSON_Delete);
}

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

const char* radio_state_str(radio_cc1101::RadioState s) {
    using radio_cc1101::RadioState;
    switch (s) {
    case RadioState::Uninitialized:
        return "Uninitialized";
    case RadioState::Idle:
        return "Idle";
    case RadioState::Receiving:
        return "Receiving";
    case RadioState::Error:
        return "Error";
    }
    return "Unknown";
}

const char* mqtt_state_str(mqtt_service::MqttState s) {
    using mqtt_service::MqttState;
    switch (s) {
    case MqttState::Uninitialized:
        return "Uninitialized";
    case MqttState::Disconnected:
        return "Disconnected";
    case MqttState::Connecting:
        return "Connecting";
    case MqttState::Connected:
        return "Connected";
    case MqttState::Error:
        return "Error";
    }
    return "Unknown";
}

const char* wifi_state_str(wifi_manager::WifiState s) {
    using wifi_manager::WifiState;
    switch (s) {
    case WifiState::Uninitialized:
        return "Uninitialized";
    case WifiState::Disconnected:
        return "Disconnected";
    case WifiState::Connecting:
        return "Connecting";
    case WifiState::Connected:
        return "Connected";
    case WifiState::ApMode:
        return "ApMode";
    }
    return "Unknown";
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
    return root;
}

cJSON* build_summary_json(const diagnostics_service::DiagnosticsSnapshot& snap) {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return nullptr;
    }
    cJSON_AddStringToObject(root, "health_state",
                            health_monitor::HealthMonitor::state_to_string(snap.health.state));
    cJSON_AddNumberToObject(root, "health_warning_count",
                            static_cast<double>(snap.health.warning_count));
    cJSON_AddNumberToObject(root, "health_error_count",
                            static_cast<double>(snap.health.error_count));
    cJSON_AddStringToObject(root, "health_last_warning", snap.health.last_warning_msg.c_str());
    cJSON_AddStringToObject(root, "health_last_error", snap.health.last_error_msg.c_str());
    cJSON_AddStringToObject(root, "wifi_state", wifi_state_str(snap.wifi.state));
    cJSON_AddStringToObject(root, "mqtt_state", mqtt_state_str(snap.mqtt.state));
    cJSON_AddNumberToObject(root, "mqtt_outbox_depth",
                            static_cast<double>(snap.mqtt.outbox_depth));
    cJSON_AddNumberToObject(root, "mqtt_outbox_capacity",
                            static_cast<double>(snap.mqtt.outbox_capacity));
    cJSON_AddBoolToObject(root, "mqtt_held_item", snap.mqtt.held_item);
    cJSON_AddNumberToObject(root, "mqtt_retry_failure_count",
                            static_cast<double>(snap.mqtt.retry_failure_count));
    cJSON_AddStringToObject(root, "radio_state", radio_state_str(snap.radio_state));
    cJSON_AddNumberToObject(root, "radio_fifo_overflows",
                            static_cast<double>(snap.radio.fifo_overflows));
    cJSON_AddNumberToObject(root, "radio_spi_errors", static_cast<double>(snap.radio.spi_errors));
    cJSON_AddNumberToObject(root, "uptime_s", static_cast<double>(snap.metrics.uptime_s));
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
    if (!config_store::ConfigStore::instance().is_initialized() ||
        !config_store::ConfigStore::instance().is_loaded()) {
        cJSON* err = cJSON_CreateObject();
        if (err) {
            cJSON_AddStringToObject(err, "error", "config_not_loaded");
        }
        return err;
    }
    return build_redacted_config_json(config_store::ConfigStore::instance().config());
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
    cJSON_AddNumberToObject(root.get(), "generated_at_epoch_s",
                            static_cast<double>(std::time(nullptr)));

    add_owned_item(root.get(), "summary", build_summary_json(diag_res.value()));
    add_owned_item(root.get(), "diagnostics", build_diagnostics_json(diag_res.value()));
    add_owned_item(root.get(), "metrics", build_metrics_json(metrics_res.value()));
    add_owned_item(root.get(), "health", build_health_json(health_res.value()));
    add_owned_item(root.get(), "config", build_config_or_error_json());
    add_owned_item(root.get(), "logs", build_logs_json());
    add_owned_item(root.get(), "meters", build_meters_json());
    add_owned_item(root.get(), "ota", build_ota_json());

    JsonStringPtr printed(cJSON_PrintUnformatted(root.get()), cJSON_free);
    if (!printed) {
        return common::Result<std::string>::error(common::ErrorCode::Unknown);
    }
    return common::Result<std::string>::ok(std::string(printed.get()));
}

} // namespace support_bundle_service
