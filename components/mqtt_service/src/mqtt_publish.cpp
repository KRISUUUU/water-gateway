#include "mqtt_service/mqtt_publish.hpp"

#include <algorithm>
#include <cstdarg>
#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace mqtt_service {

namespace {

template <size_t N> bool copy_cstr(char (&dest)[N], const char* src) {
    if (!src) {
        dest[0] = '\0';
        return true;
    }
    const size_t len = std::strlen(src);
    if (len >= N) {
        return false;
    }
    std::memcpy(dest, src, len + 1U);
    return true;
}

bool append_literal(char* dest, size_t capacity, size_t& used, const char* text) {
    const size_t len = std::strlen(text);
    if (used + len >= capacity) {
        return false;
    }
    std::memcpy(dest + used, text, len);
    used += len;
    dest[used] = '\0';
    return true;
}

bool append_format(char* dest, size_t capacity, size_t& used, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(dest + used, capacity - used, format, args);
    va_end(args);
    if (written < 0) {
        return false;
    }
    const size_t required = static_cast<size_t>(written);
    if (used + required >= capacity) {
        return false;
    }
    used += required;
    return true;
}

bool append_json_string(char* dest, size_t capacity, size_t& used, const char* value) {
    if (!append_literal(dest, capacity, used, "\"")) {
        return false;
    }

    const char* safe = value ? value : "";
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(safe); *p != '\0'; ++p) {
        switch (*p) {
        case '\"':
            if (!append_literal(dest, capacity, used, "\\\"")) {
                return false;
            }
            break;
        case '\\':
            if (!append_literal(dest, capacity, used, "\\\\")) {
                return false;
            }
            break;
        case '\b':
            if (!append_literal(dest, capacity, used, "\\b")) {
                return false;
            }
            break;
        case '\f':
            if (!append_literal(dest, capacity, used, "\\f")) {
                return false;
            }
            break;
        case '\n':
            if (!append_literal(dest, capacity, used, "\\n")) {
                return false;
            }
            break;
        case '\r':
            if (!append_literal(dest, capacity, used, "\\r")) {
                return false;
            }
            break;
        case '\t':
            if (!append_literal(dest, capacity, used, "\\t")) {
                return false;
            }
            break;
        default:
            if (*p < 0x20U) {
                if (!append_format(dest, capacity, used, "\\u%04X", static_cast<unsigned int>(*p))) {
                    return false;
                }
            } else {
                if (used + 1U >= capacity) {
                    return false;
                }
                dest[used++] = static_cast<char>(*p);
                dest[used] = '\0';
            }
            break;
        }
    }

    return append_literal(dest, capacity, used, "\"");
}

bool append_hex(char* dest, size_t capacity, size_t& used, const uint8_t* data, size_t length) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    if (!append_literal(dest, capacity, used, "\"")) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        if (used + 2U >= capacity) {
            return false;
        }
        dest[used++] = kHex[(data[i] >> 4U) & 0x0FU];
        dest[used++] = kHex[data[i] & 0x0FU];
        dest[used] = '\0';
    }
    return append_literal(dest, capacity, used, "\"");
}

bool build_topic(char* dest, size_t capacity, const char* prefix, const char* device_id,
                 const char* suffix) {
    if (!prefix || !device_id || !suffix || prefix[0] == '\0' || device_id[0] == '\0') {
        return false;
    }
    const int written = std::snprintf(dest, capacity, "%s/%s/%s", prefix, device_id, suffix);
    return written >= 0 && static_cast<size_t>(written) < capacity;
}

common::ErrorCode overflow_code() {
    return common::ErrorCode::BufferFull;
}

common::Result<SerializedPublishMessage> build_telemetry_message(const MqttPublishCommand& command) {
    SerializedPublishMessage message{};
    if (!build_topic(message.topic, sizeof(message.topic), command.topic_prefix, command.device_id,
                     "telemetry")) {
        return common::Result<SerializedPublishMessage>::error(overflow_code());
    }

    size_t used = 0;
    if (!append_literal(message.payload, sizeof(message.payload), used, "{") ||
        !append_format(message.payload, sizeof(message.payload), used,
                       "\"uptime_s\":%" PRIu32 ",\"free_heap_bytes\":%" PRIu32
                       ",\"min_free_heap_bytes\":%" PRIu32 ",\"wifi_rssi_dbm\":%" PRId8
                       ",\"mqtt_state\":",
                       command.uptime_s, command.free_heap_bytes, command.min_free_heap_bytes,
                       command.wifi_rssi_dbm) ||
        !append_json_string(message.payload, sizeof(message.payload), used, command.mqtt_state) ||
        !append_literal(message.payload, sizeof(message.payload), used, ",\"radio_state\":") ||
        !append_json_string(message.payload, sizeof(message.payload), used, command.radio_state) ||
        !append_format(message.payload, sizeof(message.payload), used,
                       ",\"frames_received\":%" PRIu32 ",\"frames_published\":%" PRIu32
                       ",\"frames_duplicate\":%" PRIu32 ",\"frames_crc_fail\":%" PRIu32
                       ",\"mqtt_publishes\":%" PRIu32 ",\"mqtt_failures\":%" PRIu32
                       ",\"timestamp\":",
                       command.frames_received, command.frames_published,
                       command.frames_duplicate, command.frames_crc_fail, command.mqtt_publishes,
                       command.mqtt_failures) ||
        !append_json_string(message.payload, sizeof(message.payload), used, command.timestamp) ||
        !append_literal(message.payload, sizeof(message.payload), used, "}")) {
        return common::Result<SerializedPublishMessage>::error(overflow_code());
    }
    return common::Result<SerializedPublishMessage>::ok(message);
}

common::Result<SerializedPublishMessage> build_event_message(const MqttPublishCommand& command) {
    SerializedPublishMessage message{};
    if (!build_topic(message.topic, sizeof(message.topic), command.topic_prefix, command.device_id,
                     "events")) {
        return common::Result<SerializedPublishMessage>::error(overflow_code());
    }

    size_t used = 0;
    if (!append_literal(message.payload, sizeof(message.payload), used, "{\"event\":") ||
        !append_json_string(message.payload, sizeof(message.payload), used, command.event_type) ||
        !append_literal(message.payload, sizeof(message.payload), used, ",\"severity\":") ||
        !append_json_string(message.payload, sizeof(message.payload), used, command.severity) ||
        !append_literal(message.payload, sizeof(message.payload), used, ",\"message\":") ||
        !append_json_string(message.payload, sizeof(message.payload), used, command.event_message) ||
        !append_literal(message.payload, sizeof(message.payload), used, ",\"timestamp\":") ||
        !append_json_string(message.payload, sizeof(message.payload), used, command.timestamp) ||
        !append_literal(message.payload, sizeof(message.payload), used, "}")) {
        return common::Result<SerializedPublishMessage>::error(overflow_code());
    }
    return common::Result<SerializedPublishMessage>::ok(message);
}

common::Result<SerializedPublishMessage> build_raw_frame_message(const MqttPublishCommand& command) {
    if (command.raw_length > command.raw_bytes.size()) {
        return common::Result<SerializedPublishMessage>::error(common::ErrorCode::InvalidArgument);
    }

    SerializedPublishMessage message{};
    if (!build_topic(message.topic, sizeof(message.topic), command.topic_prefix, command.device_id,
                     "rf/raw")) {
        return common::Result<SerializedPublishMessage>::error(overflow_code());
    }

    size_t used = 0;
    if (!append_literal(message.payload, sizeof(message.payload), used, "{\"protocol_name\":") ||
        !append_json_string(message.payload, sizeof(message.payload), used,
                            command.protocol_name[0] ? command.protocol_name : "WMBUS_T") ||
        !append_literal(message.payload, sizeof(message.payload), used, ",\"raw_hex\":") ||
        !append_hex(message.payload, sizeof(message.payload), used, command.raw_bytes.data(),
                    command.raw_length) ||
        !append_format(message.payload, sizeof(message.payload), used,
                       ",\"frame_length\":%" PRIu16 ",\"rssi_dbm\":%" PRId8
                       ",\"lqi\":%" PRIu8 ",\"crc_ok\":%s,\"radio_crc_available\":%s"
                       ",\"decoded_ok\":%s,\"raw_frame_contract_valid\":%s"
                       ",\"manufacturer_id\":%" PRIu16 ",\"device_id\":%" PRIu32 ",\"meter_key\":",
                       command.raw_length, command.rssi_dbm, command.lqi,
                       command.crc_ok ? "true" : "false",
                       command.radio_crc_available ? "true" : "false",
                       command.decoded_ok ? "true" : "false",
                       command.raw_frame_contract_valid ? "true" : "false",
                       command.manufacturer_id, command.wmbus_device_id) ||
        !append_json_string(message.payload, sizeof(message.payload), used, command.meter_key) ||
        !append_literal(message.payload, sizeof(message.payload), used, ",\"timestamp\":") ||
        !append_json_string(message.payload, sizeof(message.payload), used, command.timestamp) ||
        !append_format(message.payload, sizeof(message.payload), used, ",\"rx_count\":%" PRIu32 "}",
                       command.rx_count)) {
        return common::Result<SerializedPublishMessage>::error(overflow_code());
    }
    return common::Result<SerializedPublishMessage>::ok(message);
}

common::Result<SerializedPublishMessage> build_prios_frame_message(
    const MqttPublishCommand& command) {
    SerializedPublishMessage message{};
    if (!build_topic(message.topic, sizeof(message.topic), command.topic_prefix, command.device_id,
                     "prios/raw")) {
        return common::Result<SerializedPublishMessage>::error(overflow_code());
    }

    size_t used = 0;
    if (!append_literal(message.payload, sizeof(message.payload), used,
                        "{\"protocol_name\":\"PRIOS_R3\",\"vendor\":\"Techem\",\"support_level\":\"identity_only_capture\",\"decoded_ok\":false,\"reading_decode_available\":false,\"meter_key\":") ||
        !append_json_string(message.payload, sizeof(message.payload), used, command.meter_key) ||
        !append_literal(message.payload, sizeof(message.payload), used,
                        ",\"display_prefix_hex\":") ||
        !append_json_string(message.payload, sizeof(message.payload), used,
                            command.prios_display_hex) ||
        !append_format(message.payload, sizeof(message.payload), used,
                       ",\"captured_length\":%" PRIu16 ",\"rssi_dbm\":%" PRId8
                       ",\"lqi\":%" PRIu8 ",\"manchester_enabled\":%s,\"timestamp\":",
                       command.raw_length,  // raw_length re-used as captured_length for PRIOS
                       command.rssi_dbm, command.lqi,
                       command.prios_manchester_enabled ? "true" : "false") ||
        !append_json_string(message.payload, sizeof(message.payload), used, command.timestamp) ||
        !append_literal(message.payload, sizeof(message.payload), used, "}")) {
        return common::Result<SerializedPublishMessage>::error(overflow_code());
    }
    return common::Result<SerializedPublishMessage>::ok(message);
}

} // namespace

common::Result<SerializedPublishMessage> serialize_publish_command(
    const MqttPublishCommand& command) {
    switch (command.type) {
    case PublishCommandType::Telemetry:
        return build_telemetry_message(command);
    case PublishCommandType::Event:
        return build_event_message(command);
    case PublishCommandType::RawFrame:
        return build_raw_frame_message(command);
    case PublishCommandType::PriosFrame:
        return build_prios_frame_message(command);
    }
    return common::Result<SerializedPublishMessage>::error(common::ErrorCode::InvalidArgument);
}

common::Result<MqttPublishCommand> make_telemetry_command(
    const char* topic_prefix, const char* device_id, uint32_t uptime_s, uint32_t free_heap_bytes,
    uint32_t min_free_heap_bytes, int8_t wifi_rssi_dbm, const char* mqtt_state,
    const char* radio_state, uint32_t frames_received, uint32_t frames_published,
    uint32_t frames_duplicate, uint32_t frames_crc_fail, uint32_t mqtt_publishes,
    uint32_t mqtt_failures, const char* timestamp) {
    MqttPublishCommand command{};
    command.type = PublishCommandType::Telemetry;
    if (!copy_cstr(command.topic_prefix, topic_prefix) || !copy_cstr(command.device_id, device_id) ||
        !copy_cstr(command.mqtt_state, mqtt_state) || !copy_cstr(command.radio_state, radio_state) ||
        !copy_cstr(command.timestamp, timestamp)) {
        return common::Result<MqttPublishCommand>::error(overflow_code());
    }
    command.uptime_s = uptime_s;
    command.free_heap_bytes = free_heap_bytes;
    command.min_free_heap_bytes = min_free_heap_bytes;
    command.wifi_rssi_dbm = wifi_rssi_dbm;
    command.frames_received = frames_received;
    command.frames_published = frames_published;
    command.frames_duplicate = frames_duplicate;
    command.frames_crc_fail = frames_crc_fail;
    command.mqtt_publishes = mqtt_publishes;
    command.mqtt_failures = mqtt_failures;
    return common::Result<MqttPublishCommand>::ok(command);
}

common::Result<MqttPublishCommand> make_event_command(const char* topic_prefix,
                                                      const char* device_id,
                                                      const char* event_type,
                                                      const char* severity,
                                                      const char* message,
                                                      const char* timestamp) {
    MqttPublishCommand command{};
    command.type = PublishCommandType::Event;
    if (!copy_cstr(command.topic_prefix, topic_prefix) || !copy_cstr(command.device_id, device_id) ||
        !copy_cstr(command.event_type, event_type) || !copy_cstr(command.severity, severity) ||
        !copy_cstr(command.event_message, message) || !copy_cstr(command.timestamp, timestamp)) {
        return common::Result<MqttPublishCommand>::error(overflow_code());
    }
    return common::Result<MqttPublishCommand>::ok(command);
}

common::Result<MqttPublishCommand> make_raw_frame_command(
    const char* topic_prefix, const char* device_id, const uint8_t* raw_bytes, uint16_t raw_length,
    int8_t rssi_dbm, uint8_t lqi, bool crc_ok, bool radio_crc_available,
    uint16_t manufacturer_id, uint32_t wmbus_device_id, const char* meter_key,
    const char* timestamp, uint32_t rx_count, bool decoded_ok, bool raw_frame_contract_valid) {
    if (!raw_bytes || raw_length > kPublishMaxRawBytes) {
        return common::Result<MqttPublishCommand>::error(common::ErrorCode::InvalidArgument);
    }

    MqttPublishCommand command{};
    command.type = PublishCommandType::RawFrame;
    if (!copy_cstr(command.topic_prefix, topic_prefix) || !copy_cstr(command.device_id, device_id) ||
        !copy_cstr(command.meter_key, meter_key) || !copy_cstr(command.timestamp, timestamp)) {
        return common::Result<MqttPublishCommand>::error(overflow_code());
    }
    std::copy(raw_bytes, raw_bytes + raw_length, command.raw_bytes.begin());
    command.raw_length = raw_length;
    command.rssi_dbm = rssi_dbm;
    command.lqi = lqi;
    command.crc_ok = crc_ok;
    command.radio_crc_available = radio_crc_available;
    command.manufacturer_id = manufacturer_id;
    command.wmbus_device_id = wmbus_device_id;
    command.rx_count = rx_count;
    command.decoded_ok = decoded_ok;
    command.raw_frame_contract_valid = raw_frame_contract_valid;
    // T-mode protocol identity.
    copy_cstr(command.protocol_name, "WMBUS_T");
    return common::Result<MqttPublishCommand>::ok(command);
}

common::Result<MqttPublishCommand> make_prios_frame_command(
    const char* topic_prefix, const char* device_id, const char* meter_key,
    const char* display_prefix_hex, uint16_t captured_length, int8_t rssi_dbm, uint8_t lqi,
    bool manchester_enabled, const char* timestamp) {
    MqttPublishCommand command{};
    command.type = PublishCommandType::PriosFrame;
    if (!copy_cstr(command.topic_prefix, topic_prefix) ||
        !copy_cstr(command.device_id, device_id)       ||
        !copy_cstr(command.meter_key, meter_key)        ||
        !copy_cstr(command.prios_display_hex, display_prefix_hex) ||
        !copy_cstr(command.timestamp, timestamp)        ||
        !copy_cstr(command.protocol_name, "PRIOS_R3")  ||
        !copy_cstr(command.vendor, "Techem")) {
        return common::Result<MqttPublishCommand>::error(overflow_code());
    }
    // raw_length is re-used as captured_length for PRIOS (no raw bytes stored).
    command.raw_length            = captured_length;
    command.rssi_dbm              = rssi_dbm;
    command.lqi                   = lqi;
    command.prios_manchester_enabled = manchester_enabled;
    return common::Result<MqttPublishCommand>::ok(command);
}

} // namespace mqtt_service
