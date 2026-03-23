#define HOST_TEST_BUILD
#include "host_test_stubs.hpp"
#include "mqtt_service/mqtt_topics.hpp"
#include "mqtt_service/mqtt_payloads.hpp"
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

static void test_payload_raw_frame() {
    auto p = payload_raw_frame("2C4493", 3, -65, 45, true, "2025-01-15T12:00:00Z", 42);
    assert(p.find("\"raw_hex\":\"2C4493\"") != std::string::npos);
    assert(p.find("\"frame_length\":3") != std::string::npos);
    assert(p.find("\"rssi_dbm\":-65") != std::string::npos);
    assert(p.find("\"lqi\":45") != std::string::npos);
    assert(p.find("\"crc_ok\":true") != std::string::npos);
    assert(p.find("\"rx_count\":42") != std::string::npos);
    printf("  PASS: payload_raw_frame\n");
}

static void test_payload_event() {
    auto p = payload_event("radio_error", "warning", "FIFO overflow", "2025-01-15T12:00:00Z");
    assert(p.find("\"event\":\"radio_error\"") != std::string::npos);
    assert(p.find("\"severity\":\"warning\"") != std::string::npos);
    assert(p.find("\"message\":\"FIFO overflow\"") != std::string::npos);
    printf("  PASS: payload_event\n");
}

static void test_payload_telemetry() {
    auto p = payload_telemetry(86400, 120000, 95000, -55, "connected", "rx_active",
                                4523, 4400, 120, 3, 4420, 2, "2025-01-15T12:00:00Z");
    assert(p.find("\"uptime_s\":86400") != std::string::npos);
    assert(p.find("\"free_heap_bytes\":120000") != std::string::npos);
    assert(p.find("\"frames_received\":4523") != std::string::npos);
    assert(p.find("\"mqtt_state\":\"connected\"") != std::string::npos);
    printf("  PASS: payload_telemetry\n");
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
    test_payload_raw_frame();
    test_payload_event();
    test_payload_telemetry();
    printf("All MQTT payload tests passed.\n");
    return 0;
}
