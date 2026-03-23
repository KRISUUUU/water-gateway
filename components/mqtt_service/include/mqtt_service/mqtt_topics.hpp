#pragma once

#include <string>

namespace mqtt_service {

std::string topic_status(const std::string& prefix, const std::string& node);
std::string topic_telemetry(const std::string& prefix, const std::string& node);
std::string topic_events(const std::string& prefix, const std::string& node);
std::string topic_raw_frames(const std::string& prefix, const std::string& node);

}  // namespace mqtt_service
