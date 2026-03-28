#include "support_bundle_service/support_summary.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace support_bundle_service;

static void test_compact_summary_healthy_path() {
    diagnostics_service::DiagnosticsSnapshot diagnostics{};
    diagnostics.health.state = health_monitor::HealthState::Healthy;
    diagnostics.mqtt.state = mqtt_service::MqttState::Connected;
    diagnostics.mqtt.outbox_capacity = 32;
    diagnostics.radio_state = radio_cc1101::RadioState::Receiving;

    ota_manager::OtaStatus ota{};
    ota.state = ota_manager::OtaState::Idle;
    std::strncpy(ota.message, "", sizeof(ota.message) - 1);

    auto summary = build_compact_support_summary(diagnostics, ota, "normal", 1700000000);
    assert(summary.generated_at_epoch_s == 1700000000);
    assert(summary.mode == "normal");
    assert(summary.health.state == "healthy");
    assert(summary.health.message == "No active health alerts.");
    assert(summary.mqtt.state == "connected");
    assert(summary.queue.state == "clear");
    assert(summary.radio.state == "receiving");
    assert(summary.ota.state == "idle");
    printf("  PASS: healthy compact summary is concise and stable\n");
}

static void test_compact_summary_problem_states() {
    diagnostics_service::DiagnosticsSnapshot diagnostics{};
    diagnostics.health.state = health_monitor::HealthState::Error;
    diagnostics.health.error_count = 2;
    diagnostics.health.last_error_msg = "MQTT retries exhausted";

    diagnostics.mqtt.state = mqtt_service::MqttState::Disconnected;
    diagnostics.mqtt.outbox_depth = 8;
    diagnostics.mqtt.outbox_capacity = 8;
    diagnostics.mqtt.held_item = true;
    diagnostics.mqtt.retry_failure_count = 3;

    diagnostics.radio_state = radio_cc1101::RadioState::Error;
    diagnostics.radio.spi_errors = 4;
    diagnostics.radio.fifo_overflows = 2;

    ota_manager::OtaStatus ota{};
    ota.state = ota_manager::OtaState::Failed;
    std::strncpy(ota.message, "Image validation failed", sizeof(ota.message) - 1);

    auto summary = build_compact_support_summary(diagnostics, ota, "provisioning", 1700000001);
    assert(summary.mode == "provisioning");
    assert(summary.health.state == "error");
    assert(summary.health.message.find("MQTT retries exhausted") != std::string::npos);
    assert(summary.mqtt.state == "disconnected");
    assert(summary.queue.state == "high_pressure");
    assert(summary.queue.message.find("held item pending") != std::string::npos);
    assert(summary.radio.state == "error");
    assert(summary.radio.message.find("SPI errors") != std::string::npos);
    assert(summary.ota.state == "error");
    assert(summary.ota.message.find("validation failed") != std::string::npos);
    printf("  PASS: degraded compact summary highlights operator-facing problems\n");
}

int main() {
    printf("=== test_support_summary ===\n");
    test_compact_summary_healthy_path();
    test_compact_summary_problem_states();
    printf("All support summary tests passed.\n");
    return 0;
}
