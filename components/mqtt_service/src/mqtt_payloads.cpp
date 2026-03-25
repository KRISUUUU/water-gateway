#include "mqtt_service/mqtt_payloads.hpp"
#include <cstring>
#include <cstdio>

namespace mqtt_service {

// JSON string escaping for safe embedding of arbitrary values.
// Handles the minimum required escapes for JSON string values.
static std::string json_escape(const char* s) {
    if (!s) return "";
    std::string out;
    out.reserve(std::strlen(s) + 16);
    for (const char* p = s; *p; ++p) {
        switch (*p) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:   out += *p; break;
        }
    }
    return out;
}

std::string payload_status_online(const char* firmware_version,
                                   const char* ip_address,
                                   const char* hostname,
                                   uint32_t uptime_s,
                                   const char* health_state) {
    char buf[384];
    std::snprintf(buf, sizeof(buf),
        R"({"online":true,"firmware_version":"%s","ip_address":"%s",)"
        R"("hostname":"%s","uptime_s":%lu,"health":"%s"})",
        json_escape(firmware_version).c_str(),
        json_escape(ip_address).c_str(),
        json_escape(hostname).c_str(),
        static_cast<unsigned long>(uptime_s),
        json_escape(health_state).c_str());
    return std::string(buf);
}

std::string payload_status_offline() {
    return R"({"online":false})";
}

std::string payload_telemetry(uint32_t uptime_s,
                               uint32_t free_heap_bytes,
                               uint32_t min_free_heap_bytes,
                               int8_t wifi_rssi_dbm,
                               const char* mqtt_state,
                               const char* radio_state,
                               uint32_t frames_received,
                               uint32_t frames_published,
                               uint32_t frames_duplicate,
                               uint32_t frames_crc_fail,
                               uint32_t mqtt_publishes,
                               uint32_t mqtt_failures,
                               const char* timestamp) {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
        R"({"uptime_s":%lu,"free_heap_bytes":%lu,"min_free_heap_bytes":%lu,)"
        R"("wifi_rssi_dbm":%d,"mqtt_state":"%s","radio_state":"%s",)"
        R"("frames_received":%lu,"frames_published":%lu,)"
        R"("frames_duplicate":%lu,"frames_crc_fail":%lu,)"
        R"("mqtt_publishes":%lu,"mqtt_failures":%lu,"timestamp":"%s"})",
        (unsigned long)uptime_s,
        (unsigned long)free_heap_bytes,
        (unsigned long)min_free_heap_bytes,
        (int)wifi_rssi_dbm,
        json_escape(mqtt_state).c_str(),
        json_escape(radio_state).c_str(),
        (unsigned long)frames_received,
        (unsigned long)frames_published,
        (unsigned long)frames_duplicate,
        (unsigned long)frames_crc_fail,
        (unsigned long)mqtt_publishes,
        (unsigned long)mqtt_failures,
        json_escape(timestamp).c_str());
    return std::string(buf);
}

std::string payload_event(const char* event_type,
                           const char* severity,
                           const char* message,
                           const char* timestamp) {
    char buf[320];
    std::snprintf(buf, sizeof(buf),
        R"({"event":"%s","severity":"%s","message":"%s","timestamp":"%s"})",
        json_escape(event_type).c_str(),
        json_escape(severity).c_str(),
        json_escape(message).c_str(),
        json_escape(timestamp).c_str());
    return std::string(buf);
}

std::string payload_raw_frame(const char* raw_hex,
                               uint16_t frame_length,
                               int8_t rssi_dbm,
                               uint8_t lqi,
                               bool crc_ok,
                               uint16_t manufacturer_id,
                               uint32_t device_id,
                               const char* meter_key,
                               const char* timestamp,
                               uint32_t rx_count) {
    char buf[640];
    std::snprintf(buf, sizeof(buf),
        R"({"raw_hex":"%s","frame_length":%u,"rssi_dbm":%d,)"
        R"("lqi":%u,"crc_ok":%s,"manufacturer_id":%u,"device_id":%lu,)"
        R"("meter_key":"%s","timestamp":"%s","rx_count":%lu})",
        json_escape(raw_hex).c_str(),
        (unsigned)frame_length,
        (int)rssi_dbm,
        (unsigned)lqi,
        crc_ok ? "true" : "false",
        (unsigned)manufacturer_id,
        (unsigned long)device_id,
        json_escape(meter_key).c_str(),
        json_escape(timestamp).c_str(),
        (unsigned long)rx_count);
    return std::string(buf);
}

} // namespace mqtt_service
