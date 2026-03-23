#pragma once

#include <string>

namespace mqtt_service {

// Builds MQTT topic strings from prefix and device ID.
// All topic builders are pure functions for host-testability.

std::string topic_status(const std::string& prefix, const std::string& device_id);
std::string topic_telemetry(const std::string& prefix, const std::string& device_id);
std::string topic_events(const std::string& prefix, const std::string& device_id);
std::string topic_raw_frame(const std::string& prefix, const std::string& device_id);

} // namespace mqtt_service
