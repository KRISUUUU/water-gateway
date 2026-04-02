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

const char* burst_end_reason_str(uint8_t reason_value) {
    switch (reason_value) {
    case 0:
        return "none";
    case 1:
        return "empty_polls";
    case 2:
        return "max_duration";
    }
    return "unknown";
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

std::string payload_raw_frame(const char* radio_hex, uint16_t radio_frame_length,
                              const char* canonical_hex, uint16_t canonical_frame_length,
                              bool decoded, bool raw_frame_contract_valid,
                              uint8_t burst_end_reason,
                              uint8_t first_data_byte, uint16_t payload_offset,
                              uint16_t payload_length, int8_t rssi_dbm,
                              uint8_t lqi, bool crc_ok, bool radio_crc_available,
                              uint16_t manufacturer_id,
                              uint32_t device_id, const char* meter_key, const char* timestamp,
                              uint32_t rx_count) {
    JsonPtr root = make_object();
    if (!root) {
        return "{}";
    }

    cJSON_AddStringToObject(root.get(), "radio_hex", safe_cstr(radio_hex));
    cJSON_AddNumberToObject(root.get(), "radio_frame_length",
                            static_cast<double>(radio_frame_length));
    cJSON_AddStringToObject(root.get(), "captured_hex", safe_cstr(radio_hex));
    cJSON_AddNumberToObject(root.get(), "captured_frame_length",
                            static_cast<double>(radio_frame_length));
    cJSON_AddStringToObject(root.get(), "canonical_hex", safe_cstr(canonical_hex));
    cJSON_AddNumberToObject(root.get(), "canonical_frame_length",
                            static_cast<double>(canonical_frame_length));
    cJSON_AddBoolToObject(root.get(), "decoded", decoded);
    cJSON_AddBoolToObject(root.get(), "decoded_ok", decoded);
    cJSON_AddBoolToObject(root.get(), "raw_frame_contract_valid", raw_frame_contract_valid);
    cJSON_AddStringToObject(root.get(), "burst_end_reason", burst_end_reason_str(burst_end_reason));
    cJSON_AddNumberToObject(root.get(), "first_data_byte", static_cast<double>(first_data_byte));
    cJSON_AddNumberToObject(root.get(), "payload_offset", static_cast<double>(payload_offset));
    cJSON_AddNumberToObject(root.get(), "payload_length", static_cast<double>(payload_length));
    cJSON_AddNumberToObject(root.get(), "rssi_dbm", static_cast<double>(rssi_dbm));
    cJSON_AddNumberToObject(root.get(), "lqi", static_cast<double>(lqi));
    cJSON_AddBoolToObject(root.get(), "crc_ok", crc_ok);
    cJSON_AddBoolToObject(root.get(), "radio_crc_available", radio_crc_available);
    cJSON_AddNumberToObject(root.get(), "manufacturer_id", static_cast<double>(manufacturer_id));
    cJSON_AddNumberToObject(root.get(), "device_id", static_cast<double>(device_id));
    cJSON_AddStringToObject(root.get(), "meter_key", safe_cstr(meter_key));
    cJSON_AddStringToObject(root.get(), "timestamp", safe_cstr(timestamp));
    cJSON_AddNumberToObject(root.get(), "rx_count", static_cast<double>(rx_count));
    return to_unformatted_json(root.get());
}

std::string payload_raw_frame_compact(const char* reason, uint16_t captured_frame_length,
                                      uint8_t burst_end_reason, uint8_t first_data_byte,
                                      const char* prefix_hex, uint16_t elapsed_ms,
                                      int8_t rssi_dbm, uint8_t lqi, const char* meter_key,
                                      const char* timestamp, uint32_t rx_count) {
    JsonPtr root = make_object();
    if (!root) {
        return "{}";
    }

    cJSON_AddBoolToObject(root.get(), "compact", true);
    cJSON_AddStringToObject(root.get(), "reason", safe_cstr(reason));
    cJSON_AddStringToObject(root.get(), "burst_end_reason", burst_end_reason_str(burst_end_reason));
    cJSON_AddNumberToObject(root.get(), "captured_frame_length",
                            static_cast<double>(captured_frame_length));
    cJSON_AddNumberToObject(root.get(), "first_data_byte", static_cast<double>(first_data_byte));
    cJSON_AddStringToObject(root.get(), "prefix_hex", safe_cstr(prefix_hex));
    cJSON_AddNumberToObject(root.get(), "elapsed_ms", static_cast<double>(elapsed_ms));
    cJSON_AddNumberToObject(root.get(), "rssi_dbm", static_cast<double>(rssi_dbm));
    cJSON_AddNumberToObject(root.get(), "lqi", static_cast<double>(lqi));
    cJSON_AddStringToObject(root.get(), "meter_key", safe_cstr(meter_key));
    cJSON_AddStringToObject(root.get(), "timestamp", safe_cstr(timestamp));
    cJSON_AddNumberToObject(root.get(), "rx_count", static_cast<double>(rx_count));
    return to_unformatted_json(root.get());
}

} // namespace mqtt_service
