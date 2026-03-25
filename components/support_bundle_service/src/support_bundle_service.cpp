#include "support_bundle_service/support_bundle_service.hpp"
#include "config_store/config_store.hpp"
#include "diagnostics_service/diagnostics_service.hpp"
#include "health_monitor/health_monitor.hpp"
#include "meter_registry/meter_registry.hpp"
#include "metrics_service/metrics_service.hpp"
#include "ota_manager/ota_manager.hpp"
#include "persistent_log_buffer/persistent_log_buffer.hpp"

#include <cstdio>
#include <string>
#include <string_view>

namespace support_bundle_service {

namespace {

std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20U) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x",
                              static_cast<unsigned int>(static_cast<unsigned char>(c)));
                out += buf;
            } else {
                out += c;
            }
            break;
        }
    }
    return out;
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

std::string redacted_config_json(const config_store::AppConfig& c) {
    char buf[128];
    std::string out;
    out.reserve(1024);
    out += '{';

    std::snprintf(buf, sizeof(buf), "\"version\":%lu,", static_cast<unsigned long>(c.version));
    out += buf;

    out += "\"device\":{";
    out += "\"name\":\"";
    out += json_escape(c.device.name);
    out += "\",\"hostname\":\"";
    out += json_escape(c.device.hostname);
    out += "\"},";

    out += "\"wifi\":{";
    out += "\"ssid\":\"";
    out += json_escape(c.wifi.ssid);
    out += "\",\"password\":\"";
    out += config_store::kRedactedValue;
    std::snprintf(buf, sizeof(buf), "\",\"max_retries\":%u},",
                  static_cast<unsigned int>(c.wifi.max_retries));
    out += buf;

    out += "\"mqtt\":{";
    std::snprintf(buf, sizeof(buf), "\"enabled\":%s,\"port\":%u,\"qos\":%u,\"use_tls\":%s,",
                  c.mqtt.enabled ? "true" : "false", static_cast<unsigned int>(c.mqtt.port),
                  static_cast<unsigned int>(c.mqtt.qos), c.mqtt.use_tls ? "true" : "false");
    out += buf;
    out += "\"host\":\"";
    out += json_escape(c.mqtt.host);
    out += "\",\"username\":\"";
    out += config_store::kRedactedValue;
    out += "\",\"password\":\"";
    out += config_store::kRedactedValue;
    out += "\",\"prefix\":\"";
    out += json_escape(c.mqtt.prefix);
    out += "\",\"client_id\":\"";
    out += json_escape(c.mqtt.client_id);
    out += "\"},";

    out += "\"radio\":{";
    std::snprintf(buf, sizeof(buf), "\"frequency_khz\":%lu,\"data_rate\":%u,\"auto_recovery\":%s},",
                  static_cast<unsigned long>(c.radio.frequency_khz),
                  static_cast<unsigned int>(c.radio.data_rate),
                  c.radio.auto_recovery ? "true" : "false");
    out += buf;

    std::snprintf(buf, sizeof(buf), "\"logging\":{\"level\":%u},",
                  static_cast<unsigned int>(c.logging.level));
    out += buf;

    out += "\"auth\":{";
    out += "\"admin_password_hash\":\"";
    out += config_store::kRedactedValue;
    std::snprintf(buf, sizeof(buf), "\",\"session_timeout_s\":%lu}",
                  static_cast<unsigned long>(c.auth.session_timeout_s));
    out += buf;

    out += '}';
    return out;
}

std::string metrics_json(const metrics_service::RuntimeMetrics& m) {
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "{\"uptime_s\":%lu,\"free_heap_bytes\":%lu,"
                  "\"min_free_heap_bytes\":%lu,\"largest_free_block\":%lu}",
                  static_cast<unsigned long>(m.uptime_s),
                  static_cast<unsigned long>(m.free_heap_bytes),
                  static_cast<unsigned long>(m.min_free_heap_bytes),
                  static_cast<unsigned long>(m.largest_free_block));
    return std::string(buf);
}

std::string health_json(const health_monitor::HealthSnapshot& h) {
    char buf[128];
    std::string out;
    out += '{';
    out += "\"state\":\"";
    out += health_monitor::HealthMonitor::state_to_string(h.state);
    out += '"';
    std::snprintf(buf, sizeof(buf),
                  ",\"warning_count\":%lu,\"error_count\":%lu,"
                  "\"uptime_s\":%llu,",
                  static_cast<unsigned long>(h.warning_count),
                  static_cast<unsigned long>(h.error_count),
                  static_cast<unsigned long long>(h.uptime_s));
    out += buf;
    out += "\"last_warning_msg\":\"";
    out += json_escape(h.last_warning_msg);
    out += "\",\"last_error_msg\":\"";
    out += json_escape(h.last_error_msg);
    out += "\"}";
    return out;
}

std::string logs_json() {
    const auto lines = persistent_log_buffer::PersistentLogBuffer::instance().lines();
    std::string out;
    out.reserve(lines.size() * 64 + 8);
    out += '[';
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            out += ',';
        }
        const auto& e = lines[i];
        char buf[48];
        std::snprintf(buf, sizeof(buf), "{\"timestamp_us\":%lld,\"severity\":\"%s\",",
                      static_cast<long long>(e.timestamp_us), log_severity_str(e.severity));
        out += buf;
        out += "\"message\":\"";
        out += json_escape(e.message);
        out += "\"}";
    }
    out += ']';
    return out;
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
    const diagnostics_service::DiagnosticsSnapshot& diag_snap = diag_res.value();
    std::string diagnostics_json = diagnostics_service::DiagnosticsService::to_json(diag_snap);

    auto metrics_res = metrics_service::MetricsService::instance().snapshot();
    if (metrics_res.is_error()) {
        return common::Result<std::string>::error(metrics_res.error());
    }

    auto health_res = health_monitor::HealthMonitor::instance().snapshot();
    if (health_res.is_error()) {
        return common::Result<std::string>::error(health_res.error());
    }

    std::string config_section;
    if (!config_store::ConfigStore::instance().is_initialized() ||
        !config_store::ConfigStore::instance().is_loaded()) {
        config_section = "{\"error\":\"config_not_loaded\"}";
    } else {
        config_section = redacted_config_json(config_store::ConfigStore::instance().config());
    }

    std::string out;
    out.reserve(diagnostics_json.size() + config_section.size() + 512);
    out += '{';
    out += "\"format\":\"water-gateway-support-bundle\",\"format_version\":1,";
    out += "\"diagnostics\":";
    out += diagnostics_json;
    out += ",\"metrics\":";
    out += metrics_json(metrics_res.value());
    out += ",\"health\":";
    out += health_json(health_res.value());
    out += ",\"config\":";
    out += config_section;
    out += ",\"logs\":";
    out += logs_json();
    const auto detected = meter_registry::MeterRegistry::instance().detected_meters();
    const auto watchlist = meter_registry::MeterRegistry::instance().watchlist();
    char meter_buf[96];
    std::snprintf(meter_buf, sizeof(meter_buf),
                  ",\"meters\":{\"detected_count\":%lu,\"watchlist_count\":%lu}",
                  static_cast<unsigned long>(detected.size()),
                  static_cast<unsigned long>(watchlist.size()));
    out += meter_buf;
    const auto ota = ota_manager::OtaManager::instance().status();
    char ota_buf[320];
    std::snprintf(ota_buf, sizeof(ota_buf),
                  ",\"ota\":{\"state\":\"%s\",\"progress_pct\":%u,\"message\":\"%s\","
                  "\"current_version\":\"%s\"}",
                  ota_state_str(ota.state), static_cast<unsigned int>(ota.progress_pct),
                  json_escape(ota.message).c_str(), json_escape(ota.current_version).c_str());
    out += ota_buf;
    out += '}';

    return common::Result<std::string>::ok(std::move(out));
}

} // namespace support_bundle_service
