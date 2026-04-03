#include "host_test_stubs.hpp"
#include "mqtt_service/mqtt_publish.hpp"
#include "mqtt_service/mqtt_topics.hpp"
#include "mqtt_service/mqtt_payloads.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <string>

using namespace mqtt_service;

static void test_topic_status() {
    auto t = topic_status("wmbus-gw", "a1b2c3");
    assert(t == "wmbus-gw/a1b2c3/status");
    printf("  PASS: topic_status\n");
}

static void test_topic_telemetry() {
    auto t = topic_telemetry("wmbus-gw", "a1b2c3");
    assert(t == "wmbus-gw/a1b2c3/telemetry");
    printf("  PASS: topic_telemetry\n");
}

static void test_topic_events() {
    auto t = topic_events("wmbus-gw", "a1b2c3");
    assert(t == "wmbus-gw/a1b2c3/events");
    printf("  PASS: topic_events\n");
}

static void test_topic_raw_frame() {
    auto t = topic_raw_frame("wmbus-gw", "a1b2c3");
    assert(t == "wmbus-gw/a1b2c3/rf/raw");
    printf("  PASS: topic_raw_frame\n");
}

static void test_custom_prefix() {
    auto t = topic_status("my-prefix", "device1");
    assert(t == "my-prefix/device1/status");
    printf("  PASS: custom prefix\n");
}

static void test_payload_status_online() {
    auto p = payload_status_online("1.0.0", "192.168.1.100", "wmbus-gw", 3600, "healthy");
    assert(p.find("\"online\":true") != std::string::npos);
    assert(p.find("\"firmware_version\":\"1.0.0\"") != std::string::npos);
    assert(p.find("\"ip_address\":\"192.168.1.100\"") != std::string::npos);
    assert(p.find("\"health\":\"healthy\"") != std::string::npos);
    assert(p.find("\"uptime_s\":3600") != std::string::npos);
    printf("  PASS: payload_status_online\n");
}

static void test_payload_status_offline() {
    auto p = payload_status_offline();
    assert(p.find("\"online\":false") != std::string::npos);
    printf("  PASS: payload_status_offline\n");
}

static void test_telemetry_command_mapping() {
    auto command_res =
        make_telemetry_command("wmbus-gw", "a1b2c3", 86400, 120000, 95000, -55, "connected",
                               "rx_active", 4523, 4400, 120, 3, 4420, 2,
                               "2025-01-15T12:00:00Z");
    assert(command_res.is_ok());
    auto serialized = serialize_publish_command(command_res.value());
    assert(serialized.is_ok());
    const auto& message = serialized.value();
    assert(std::string(message.topic) == "wmbus-gw/a1b2c3/telemetry");
    const std::string payload = message.payload;
    assert(payload.find("\"uptime_s\":86400") != std::string::npos);
    assert(payload.find("\"free_heap_bytes\":120000") != std::string::npos);
    assert(payload.find("\"frames_received\":4523") != std::string::npos);
    assert(payload.find("\"mqtt_state\":\"connected\"") != std::string::npos);
    assert(payload.size() < kPublishPayloadCapacity);
    printf("  PASS: telemetry command mapping\n");
}

static void test_event_command_mapping() {
    auto command_res = make_event_command("wmbus-gw", "a1b2c3", "radio_error", "warning",
                                          "FIFO overflow", "2025-01-15T12:00:00Z");
    assert(command_res.is_ok());
    auto serialized = serialize_publish_command(command_res.value());
    assert(serialized.is_ok());
    const auto& message = serialized.value();
    assert(std::string(message.topic) == "wmbus-gw/a1b2c3/events");
    const std::string payload = message.payload;
    assert(payload.find("\"event\":\"radio_error\"") != std::string::npos);
    assert(payload.find("\"severity\":\"warning\"") != std::string::npos);
    assert(payload.find("\"message\":\"FIFO overflow\"") != std::string::npos);
    assert(payload.size() < kPublishPayloadCapacity);
    printf("  PASS: event command mapping\n");
}

static void test_raw_frame_command_mapping() {
    const uint8_t raw_bytes[] = {0x2C, 0x44, 0x93, 0x15};
    auto command_res = make_raw_frame_command("wmbus-gw", "a1b2c3", raw_bytes, 4, -65, 45, true,
                                              true, 0x1593, 0x12345678,
                                              "mfg:1593-id:12345678",
                                              "2025-01-15T12:00:00Z", 42, true, true);
    assert(command_res.is_ok());
    auto serialized = serialize_publish_command(command_res.value());
    assert(serialized.is_ok());
    const auto& message = serialized.value();
    assert(std::string(message.topic) == "wmbus-gw/a1b2c3/rf/raw");
    const std::string payload = message.payload;
    assert(payload.find("\"raw_hex\":\"2C449315\"") != std::string::npos);
    assert(payload.find("\"frame_length\":4") != std::string::npos);
    assert(payload.find("\"rssi_dbm\":-65") != std::string::npos);
    assert(payload.find("\"lqi\":45") != std::string::npos);
    assert(payload.find("\"crc_ok\":true") != std::string::npos);
    assert(payload.find("\"decoded_ok\":true") != std::string::npos);
    assert(payload.find("\"raw_frame_contract_valid\":true") != std::string::npos);
    assert(payload.find("\"meter_key\":\"mfg:1593-id:12345678\"") != std::string::npos);
    assert(payload.find("\"rx_count\":42") != std::string::npos);
    assert(payload.find("\"captured_hex\"") == std::string::npos);
    assert(payload.find("\"canonical_hex\"") == std::string::npos);
    assert(payload.size() < kPublishPayloadCapacity);
    printf("  PASS: raw frame command mapping\n");
}

static void test_raw_frame_command_rejects_oversize() {
    std::array<uint8_t, kPublishMaxRawBytes + 1U> raw_bytes{};
    auto command_res =
        make_raw_frame_command("wmbus-gw", "a1b2c3", raw_bytes.data(),
                               static_cast<uint16_t>(raw_bytes.size()), -70, 12, false, false, 0,
                               0, "sig:INVALID", "2026-04-02T12:00:00Z", 99, false, false);
    assert(command_res.is_error());
    printf("  PASS: raw frame oversize rejected\n");
}

static void test_serializer_escapes_event_message() {
    auto command_res = make_event_command("wmbus-gw", "a1b2c3", "radio_error", "warning",
                                          "bad \"quote\"\nline", "");
    assert(command_res.is_ok());
    auto serialized = serialize_publish_command(command_res.value());
    assert(serialized.is_ok());
    const std::string payload = serialized.value().payload;
    assert(payload.find("bad \\\"quote\\\"\\nline") != std::string::npos);
    printf("  PASS: serializer escapes event message\n");
}

int main() {
    printf("=== test_mqtt_payloads ===\n");
    test_topic_status();
    test_topic_telemetry();
    test_topic_events();
    test_topic_raw_frame();
    test_custom_prefix();
    test_payload_status_online();
    test_payload_status_offline();
    test_telemetry_command_mapping();
    test_event_command_mapping();
    test_raw_frame_command_mapping();
    test_raw_frame_command_rejects_oversize();
    test_serializer_escapes_event_message();
    printf("All MQTT payload tests passed.\n");
    return 0;
}
