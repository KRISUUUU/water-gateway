#include "support_bundle_service/support_summary.hpp"

#include <sstream>

namespace support_bundle_service {

namespace {

const char* health_state_string(health_monitor::HealthState state) {
    switch (state) {
    case health_monitor::HealthState::Starting:
        return "starting";
    case health_monitor::HealthState::Healthy:
        return "healthy";
    case health_monitor::HealthState::Warning:
        return "warning";
    case health_monitor::HealthState::Error:
        return "error";
    }
    return "unknown";
}

const char* mqtt_state_string(mqtt_service::MqttState state) {
    switch (state) {
    case mqtt_service::MqttState::Uninitialized:
        return "uninitialized";
    case mqtt_service::MqttState::Disconnected:
        return "disconnected";
    case mqtt_service::MqttState::Connecting:
        return "connecting";
    case mqtt_service::MqttState::Connected:
        return "connected";
    case mqtt_service::MqttState::Error:
        return "error";
    }
    return "unknown";
}

const char* radio_state_string(radio_cc1101::RadioState state) {
    switch (state) {
    case radio_cc1101::RadioState::Uninitialized:
        return "uninitialized";
    case radio_cc1101::RadioState::Idle:
        return "idle";
    case radio_cc1101::RadioState::Receiving:
        return "receiving";
    case radio_cc1101::RadioState::Error:
        return "error";
    }
    return "unknown";
}

std::string queue_detail_text(const mqtt_service::MqttStatus& mqtt) {
    std::ostringstream out;
    const bool has_capacity = mqtt.outbox_capacity > 0;
    if (has_capacity) {
        out << mqtt.outbox_depth << "/" << mqtt.outbox_capacity << " queued";
    } else if (mqtt.outbox_depth > 0) {
        out << mqtt.outbox_depth << " queued";
    } else {
        out << "idle";
    }
    if (mqtt.held_item) {
        out << ", held item pending";
    }
    if (mqtt.retry_failure_count > 0) {
        out << ", " << mqtt.retry_failure_count << " retry failures";
    } else if (mqtt.retry_count > 0) {
        out << ", " << mqtt.retry_count << " retries";
    }
    return out.str();
}

} // namespace

SupportHealthSummary summarize_health(const health_monitor::HealthSnapshot& health) {
    SupportHealthSummary summary{};
    summary.state = health_state_string(health.state);
    summary.warning_count = health.warning_count;
    summary.error_count = health.error_count;
    if (health.error_count > 0) {
        summary.state = "error";
        summary.message = std::to_string(health.error_count) + " error(s): " +
                          (health.last_error_msg.empty() ? "check diagnostics"
                                                         : health.last_error_msg);
        return summary;
    }
    if (health.warning_count > 0) {
        summary.state = "warning";
        summary.message = std::to_string(health.warning_count) + " warning(s): " +
                          (health.last_warning_msg.empty() ? "check diagnostics"
                                                           : health.last_warning_msg);
        return summary;
    }
    if (health.state == health_monitor::HealthState::Starting) {
        summary.message = "Health monitoring is still starting up.";
        return summary;
    }
    summary.state = "healthy";
    summary.message = "No active health alerts.";
    return summary;
}

SupportSummaryEntry summarize_mqtt(const mqtt_service::MqttStatus& mqtt) {
    SupportSummaryEntry summary{};
    summary.state = mqtt_state_string(mqtt.state);
    switch (mqtt.state) {
    case mqtt_service::MqttState::Connected:
        if (mqtt.publish_failures > 0) {
            summary.message = "Connected, but publish failures were recorded.";
        } else {
            summary.message = "Connected and ready to publish.";
        }
        break;
    case mqtt_service::MqttState::Connecting:
        summary.message = "Connecting to the broker.";
        break;
    case mqtt_service::MqttState::Disconnected:
        summary.message = "Disconnected from the broker.";
        break;
    case mqtt_service::MqttState::Error:
        summary.message = "MQTT reported an error state.";
        break;
    case mqtt_service::MqttState::Uninitialized:
        summary.message = "MQTT has not been initialized yet.";
        break;
    }
    return summary;
}

SupportQueueSummary summarize_queue(const mqtt_service::MqttStatus& mqtt) {
    SupportQueueSummary summary{};
    summary.outbox_depth = mqtt.outbox_depth;
    summary.outbox_capacity = mqtt.outbox_capacity;
    summary.held_item = mqtt.held_item;
    summary.retry_failure_count = mqtt.retry_failure_count;

    const bool high_pressure = mqtt.outbox_capacity > 0 && mqtt.outbox_depth >= mqtt.outbox_capacity;
    const bool active = mqtt.held_item || mqtt.outbox_depth > 0;

    if (high_pressure) {
        summary.state = "high_pressure";
        summary.message = "Queue pressure is high: " + queue_detail_text(mqtt);
        return summary;
    }
    if (active) {
        summary.state = "active";
        summary.message = "Queue is active: " + queue_detail_text(mqtt);
        return summary;
    }
    summary.state = "clear";
    summary.message = "Queue is clear: " + queue_detail_text(mqtt);
    return summary;
}

SupportSummaryEntry summarize_radio(radio_cc1101::RadioState state,
                                    const radio_cc1101::RadioCounters& counters) {
    SupportSummaryEntry summary{};
    summary.state = radio_state_string(state);
    if (state == radio_cc1101::RadioState::Error || counters.spi_errors > 0) {
        summary.state = "error";
        summary.message = "Radio needs attention: " + std::to_string(counters.spi_errors) +
                          " SPI errors, " + std::to_string(counters.fifo_overflows) +
                          " FIFO overflows.";
        return summary;
    }
    if (counters.fifo_overflows > 0 || counters.radio_resets > 0 || counters.radio_recoveries > 0) {
        summary.state = "warning";
        summary.message = "Radio recovered from issues: " +
                          std::to_string(counters.fifo_overflows) + " FIFO overflows, " +
                          std::to_string(counters.radio_resets) + " resets, " +
                          std::to_string(counters.radio_recoveries) + " recoveries.";
        return summary;
    }
    if (state == radio_cc1101::RadioState::Receiving) {
        summary.message = "Radio is receiving without recent recoveries.";
        return summary;
    }
    if (state == radio_cc1101::RadioState::Idle) {
        summary.message = "Radio is idle without recent recoveries.";
        return summary;
    }
    summary.message = "Radio is not initialized yet.";
    return summary;
}

SupportSummaryEntry summarize_ota(const ota_manager::OtaStatus& ota) {
    SupportSummaryEntry summary{};
    summary.state = ota_manager::ota_state_to_string(ota.state);
    switch (ota.state) {
    case ota_manager::OtaState::Idle:
        summary.message = "No OTA activity.";
        break;
    case ota_manager::OtaState::InProgress:
        summary.message = "OTA in progress at " + std::to_string(ota.progress_pct) + "%.";
        if (ota.message[0] != '\0') {
            summary.message += " ";
            summary.message += ota.message;
        }
        break;
    case ota_manager::OtaState::Validating:
        summary.message = "OTA image uploaded and awaiting validation.";
        if (ota.message[0] != '\0') {
            summary.message += " ";
            summary.message += ota.message;
        }
        break;
    case ota_manager::OtaState::Rebooting:
        summary.message = "OTA requested a reboot.";
        if (ota.message[0] != '\0') {
            summary.message += " ";
            summary.message += ota.message;
        }
        break;
    case ota_manager::OtaState::Failed:
        summary.state = "error";
        summary.message = ota.message[0] != '\0' ? ota.message : "OTA failed.";
        break;
    }
    return summary;
}

CompactSupportSummary build_compact_support_summary(
    const diagnostics_service::DiagnosticsSnapshot& diagnostics,
    const ota_manager::OtaStatus& ota,
    const char* mode,
    std::int64_t generated_at_epoch_s) {
    CompactSupportSummary summary{};
    summary.generated_at_epoch_s = generated_at_epoch_s;
    summary.mode = mode && mode[0] != '\0' ? mode : "unknown";
    summary.health = summarize_health(diagnostics.health);
    summary.mqtt = summarize_mqtt(diagnostics.mqtt);
    summary.queue = summarize_queue(diagnostics.mqtt);
    summary.radio = summarize_radio(diagnostics.radio_state, diagnostics.radio);
    summary.ota = summarize_ota(ota);
    return summary;
}

} // namespace support_bundle_service
