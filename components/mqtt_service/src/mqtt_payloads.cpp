#include "mqtt_service/mqtt_payloads.hpp"

#include <memory>
#include <string>

#include "cJSON.h"

namespace mqtt_service {

namespace {

using JsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
using JsonStringPtr = std::unique_ptr<char, decltype(&cJSON_free)>;

JsonPtr make_object() {
    return JsonPtr(cJSON_CreateObject(), cJSON_Delete);
}

const char* safe_cstr(const char* s) {
    return s ? s : "";
}

std::string to_unformatted_json(cJSON* root) {
    if (!root) {
        return "{}";
    }
    JsonStringPtr printed(cJSON_PrintUnformatted(root), cJSON_free);
    if (!printed) {
        return "{}";
    }
    return std::string(printed.get());
}

} // namespace

std::string payload_status_online(const char* firmware_version, const char* ip_address,
                                  const char* hostname, uint32_t uptime_s,
                                  const char* health_state) {
    JsonPtr root = make_object();
    if (!root) {
        return "{}";
    }

    cJSON_AddBoolToObject(root.get(), "online", true);
    cJSON_AddStringToObject(root.get(), "firmware_version", safe_cstr(firmware_version));
    cJSON_AddStringToObject(root.get(), "ip_address", safe_cstr(ip_address));
    cJSON_AddStringToObject(root.get(), "hostname", safe_cstr(hostname));
    cJSON_AddNumberToObject(root.get(), "uptime_s", static_cast<double>(uptime_s));
    cJSON_AddStringToObject(root.get(), "health", safe_cstr(health_state));
    return to_unformatted_json(root.get());
}

std::string payload_status_offline() {
    JsonPtr root = make_object();
    if (!root) {
        return "{}";
    }
    cJSON_AddBoolToObject(root.get(), "online", false);
    return to_unformatted_json(root.get());
}

std::string payload_telemetry(uint32_t uptime_s, uint32_t free_heap_bytes,
                              uint32_t min_free_heap_bytes, int8_t wifi_rssi_dbm,
                              const char* mqtt_state, const char* radio_state,
                              uint32_t frames_received, uint32_t frames_published,
                              uint32_t frames_duplicate, uint32_t frames_crc_fail,
                              uint32_t frames_dropped_queue_full,
                              uint32_t mqtt_publishes, uint32_t mqtt_failures,
                              const char* timestamp) {
    JsonPtr root = make_object();
    if (!root) {
        return "{}";
    }

    cJSON_AddNumberToObject(root.get(), "uptime_s", static_cast<double>(uptime_s));
    cJSON_AddNumberToObject(root.get(), "free_heap_bytes", static_cast<double>(free_heap_bytes));
    cJSON_AddNumberToObject(root.get(), "min_free_heap_bytes",
                            static_cast<double>(min_free_heap_bytes));
    cJSON_AddNumberToObject(root.get(), "wifi_rssi_dbm", static_cast<double>(wifi_rssi_dbm));
    cJSON_AddStringToObject(root.get(), "mqtt_state", safe_cstr(mqtt_state));
    cJSON_AddStringToObject(root.get(), "radio_state", safe_cstr(radio_state));
    cJSON_AddNumberToObject(root.get(), "frames_received", static_cast<double>(frames_received));
    cJSON_AddNumberToObject(root.get(), "frames_published", static_cast<double>(frames_published));
    cJSON_AddNumberToObject(root.get(), "frames_duplicate", static_cast<double>(frames_duplicate));
    cJSON_AddNumberToObject(root.get(), "frames_crc_fail", static_cast<double>(frames_crc_fail));
    cJSON_AddNumberToObject(root.get(), "frames_dropped_queue_full",
                            static_cast<double>(frames_dropped_queue_full));
    cJSON_AddNumberToObject(root.get(), "mqtt_publishes", static_cast<double>(mqtt_publishes));
    cJSON_AddNumberToObject(root.get(), "mqtt_failures", static_cast<double>(mqtt_failures));
    cJSON_AddStringToObject(root.get(), "timestamp", safe_cstr(timestamp));
    return to_unformatted_json(root.get());
}

std::string payload_event(const char* event_type, const char* severity, const char* message,
                          const char* timestamp) {
    JsonPtr root = make_object();
    if (!root) {
        return "{}";
    }

    cJSON_AddStringToObject(root.get(), "event", safe_cstr(event_type));
    cJSON_AddStringToObject(root.get(), "severity", safe_cstr(severity));
    cJSON_AddStringToObject(root.get(), "message", safe_cstr(message));
    cJSON_AddStringToObject(root.get(), "timestamp", safe_cstr(timestamp));
    return to_unformatted_json(root.get());
}

std::string payload_raw_frame(const char* raw_hex, uint16_t frame_length, int8_t rssi_dbm,
                              uint8_t lqi, bool crc_ok, uint16_t manufacturer_id,
                              uint32_t device_id, uint8_t device_type, const char* meter_key,
                              const char* timestamp, uint32_t rx_count) {
    JsonPtr root = make_object();
    if (!root) {
        return "{}";
    }

    cJSON_AddStringToObject(root.get(), "raw_hex", safe_cstr(raw_hex));
    cJSON_AddNumberToObject(root.get(), "frame_length", static_cast<double>(frame_length));
    cJSON_AddNumberToObject(root.get(), "rssi_dbm", static_cast<double>(rssi_dbm));
    cJSON_AddNumberToObject(root.get(), "lqi", static_cast<double>(lqi));
    cJSON_AddBoolToObject(root.get(), "crc_ok", crc_ok);
    cJSON_AddNumberToObject(root.get(), "manufacturer_id", static_cast<double>(manufacturer_id));
    cJSON_AddNumberToObject(root.get(), "device_id", static_cast<double>(device_id));
    cJSON_AddNumberToObject(root.get(), "device_type", static_cast<double>(device_type));
    cJSON_AddStringToObject(root.get(), "meter_key", safe_cstr(meter_key));
    cJSON_AddStringToObject(root.get(), "timestamp", safe_cstr(timestamp));
    cJSON_AddNumberToObject(root.get(), "rx_count", static_cast<double>(rx_count));
    return to_unformatted_json(root.get());
}

} // namespace mqtt_service
