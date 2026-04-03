#pragma once

#include "common/result.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace mqtt_service {

static constexpr size_t kPublishTopicCapacity = 128;
static constexpr size_t kPublishPayloadCapacity = 896;
static constexpr size_t kPublishTopicPartCapacity = 64;
static constexpr size_t kPublishTimestampCapacity = 40;
static constexpr size_t kPublishStateCapacity = 24;
static constexpr size_t kPublishEventTypeCapacity = 24;
static constexpr size_t kPublishSeverityCapacity = 16;
static constexpr size_t kPublishEventMessageCapacity = 160;
static constexpr size_t kPublishMeterKeyCapacity = 64;
static constexpr size_t kPublishMaxRawBytes = 290;

enum class PublishCommandType : uint8_t {
    Telemetry = 0,
    Event,
    RawFrame,
};

struct MqttPublishCommand {
    PublishCommandType type = PublishCommandType::Telemetry;
    char topic_prefix[kPublishTopicPartCapacity]{};
    char device_id[kPublishTopicPartCapacity]{};

    uint32_t uptime_s = 0;
    uint32_t free_heap_bytes = 0;
    uint32_t min_free_heap_bytes = 0;
    int8_t wifi_rssi_dbm = 0;
    char mqtt_state[kPublishStateCapacity]{};
    char radio_state[kPublishStateCapacity]{};
    uint32_t frames_received = 0;
    uint32_t frames_published = 0;
    uint32_t frames_duplicate = 0;
    uint32_t frames_crc_fail = 0;
    uint32_t mqtt_publishes = 0;
    uint32_t mqtt_failures = 0;

    char event_type[kPublishEventTypeCapacity]{};
    char severity[kPublishSeverityCapacity]{};
    char event_message[kPublishEventMessageCapacity]{};

    std::array<uint8_t, kPublishMaxRawBytes> raw_bytes{};
    uint16_t raw_length = 0;
    int8_t rssi_dbm = 0;
    uint8_t lqi = 0;
    bool crc_ok = false;
    bool radio_crc_available = false;
    uint16_t manufacturer_id = 0;
    uint32_t wmbus_device_id = 0;
    char meter_key[kPublishMeterKeyCapacity]{};
    char timestamp[kPublishTimestampCapacity]{};
    uint32_t rx_count = 0;
    bool decoded_ok = false;
    bool raw_frame_contract_valid = false;
};

struct SerializedPublishMessage {
    char topic[kPublishTopicCapacity]{};
    char payload[kPublishPayloadCapacity]{};
};

common::Result<SerializedPublishMessage> serialize_publish_command(
    const MqttPublishCommand& command);

common::Result<MqttPublishCommand> make_telemetry_command(
    const char* topic_prefix, const char* device_id, uint32_t uptime_s, uint32_t free_heap_bytes,
    uint32_t min_free_heap_bytes, int8_t wifi_rssi_dbm, const char* mqtt_state,
    const char* radio_state, uint32_t frames_received, uint32_t frames_published,
    uint32_t frames_duplicate, uint32_t frames_crc_fail, uint32_t mqtt_publishes,
    uint32_t mqtt_failures, const char* timestamp);

common::Result<MqttPublishCommand> make_event_command(const char* topic_prefix,
                                                      const char* device_id,
                                                      const char* event_type,
                                                      const char* severity,
                                                      const char* message,
                                                      const char* timestamp);

common::Result<MqttPublishCommand> make_raw_frame_command(
    const char* topic_prefix, const char* device_id, const uint8_t* raw_bytes, uint16_t raw_length,
    int8_t rssi_dbm, uint8_t lqi, bool crc_ok, bool radio_crc_available,
    uint16_t manufacturer_id, uint32_t wmbus_device_id, const char* meter_key,
    const char* timestamp, uint32_t rx_count, bool decoded_ok, bool raw_frame_contract_valid);

} // namespace mqtt_service
