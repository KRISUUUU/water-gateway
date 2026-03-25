#include "diagnostics_service/diagnostics_service.hpp"
#include "health_monitor/health_monitor.hpp"
#include "metrics_service/metrics_service.hpp"
#include "mqtt_service/mqtt_service.hpp"
#include "radio_cc1101/radio_cc1101.hpp"
#include "wifi_manager/wifi_manager.hpp"
#include <cstdio>
#include <string>
#include <string_view>

namespace diagnostics_service {

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

} // namespace

DiagnosticsService& DiagnosticsService::instance() {
    static DiagnosticsService service;
    return service;
}

common::Result<DiagnosticsSnapshot> DiagnosticsService::snapshot() const {
    DiagnosticsSnapshot s{};
    s.radio_state = radio_cc1101::RadioCc1101::instance().state();
    s.radio = radio_cc1101::RadioCc1101::instance().counters();
    s.mqtt = mqtt_service::MqttService::instance().status();
    s.wifi = wifi_manager::WifiManager::instance().status();

    auto metrics_res = metrics_service::MetricsService::instance().snapshot();
    if (metrics_res.is_error()) {
        return common::Result<DiagnosticsSnapshot>::error(metrics_res.error());
    }
    s.metrics = metrics_res.value();

    auto health_res = health_monitor::HealthMonitor::instance().snapshot();
    if (health_res.is_error()) {
        return common::Result<DiagnosticsSnapshot>::error(health_res.error());
    }
    s.health = health_res.value();

    return common::Result<DiagnosticsSnapshot>::ok(s);
}

std::string DiagnosticsService::to_json(const DiagnosticsSnapshot& snap) {
    char buf[256];
    std::string out;
    out.reserve(2048);
    out += '{';
    out += "\"radio_state\":\"";
    out += radio_state_str(snap.radio_state);
    out += "\",\"radio_counters\":{";
    std::snprintf(buf, sizeof(buf),
                  "\"frames_received\":%lu,\"frames_crc_ok\":%lu,"
                  "\"frames_crc_fail\":%lu,\"frames_incomplete\":%lu,"
                  "\"frames_dropped_too_long\":%lu,\"fifo_overflows\":%lu,"
                  "\"radio_resets\":%lu,\"radio_recoveries\":%lu,"
                  "\"spi_errors\":%lu}",
                  static_cast<unsigned long>(snap.radio.frames_received),
                  static_cast<unsigned long>(snap.radio.frames_crc_ok),
                  static_cast<unsigned long>(snap.radio.frames_crc_fail),
                  static_cast<unsigned long>(snap.radio.frames_incomplete),
                  static_cast<unsigned long>(snap.radio.frames_dropped_too_long),
                  static_cast<unsigned long>(snap.radio.fifo_overflows),
                  static_cast<unsigned long>(snap.radio.radio_resets),
                  static_cast<unsigned long>(snap.radio.radio_recoveries),
                  static_cast<unsigned long>(snap.radio.spi_errors));
    out += buf;

    out += ",\"mqtt\":{";
    std::snprintf(buf, sizeof(buf),
                  "\"state\":\"%s\",\"publish_count\":%lu,"
                  "\"publish_failures\":%lu,\"reconnect_count\":%lu,"
                  "\"last_publish_epoch_ms\":%lld,",
                  mqtt_state_str(snap.mqtt.state),
                  static_cast<unsigned long>(snap.mqtt.publish_count),
                  static_cast<unsigned long>(snap.mqtt.publish_failures),
                  static_cast<unsigned long>(snap.mqtt.reconnect_count),
                  static_cast<long long>(snap.mqtt.last_publish_epoch_ms));
    out += buf;
    out += "\"broker_uri\":\"";
    out += json_escape(snap.mqtt.broker_uri);
    out += "\"}";

    out += ",\"wifi\":{";
    std::snprintf(buf, sizeof(buf),
                  "\"state\":\"%s\",\"rssi_dbm\":%d,"
                  "\"reconnect_count\":%lu,",
                  wifi_state_str(snap.wifi.state), static_cast<int>(snap.wifi.rssi_dbm),
                  static_cast<unsigned long>(snap.wifi.reconnect_count));
    out += buf;
    out += "\"ip_address\":\"";
    out += json_escape(snap.wifi.ip_address);
    out += "\",\"ssid\":\"";
    out += json_escape(snap.wifi.ssid);
    out += "\"}";

    out += ",\"metrics\":{";
    std::snprintf(buf, sizeof(buf),
                  "\"uptime_s\":%lu,\"free_heap_bytes\":%lu,"
                  "\"min_free_heap_bytes\":%lu,\"largest_free_block\":%lu}",
                  static_cast<unsigned long>(snap.metrics.uptime_s),
                  static_cast<unsigned long>(snap.metrics.free_heap_bytes),
                  static_cast<unsigned long>(snap.metrics.min_free_heap_bytes),
                  static_cast<unsigned long>(snap.metrics.largest_free_block));
    out += buf;

    out += ",\"health\":{";
    out += "\"state\":\"";
    out += health_monitor::HealthMonitor::state_to_string(snap.health.state);
    out += '"';
    std::snprintf(buf, sizeof(buf),
                  ",\"warning_count\":%lu,\"error_count\":%lu,"
                  "\"uptime_s\":%llu,",
                  static_cast<unsigned long>(snap.health.warning_count),
                  static_cast<unsigned long>(snap.health.error_count),
                  static_cast<unsigned long long>(snap.health.uptime_s));
    out += buf;
    out += "\"last_warning_msg\":\"";
    out += json_escape(snap.health.last_warning_msg);
    out += "\",\"last_error_msg\":\"";
    out += json_escape(snap.health.last_error_msg);
    out += "\"}";

    out += '}';
    return out;
}

} // namespace diagnostics_service
