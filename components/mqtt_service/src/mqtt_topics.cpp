#include "mqtt_service/mqtt_topics.hpp"

namespace mqtt_service {

std::string topic_status(const std::string& prefix, const std::string& node) {
    return prefix + "/" + node + "/status";
}

std::string topic_telemetry(const std::string& prefix, const std::string& node) {
    return prefix + "/" + node + "/telemetry";
}

std::string topic_events(const std::string& prefix, const std::string& node) {
    return prefix + "/" + node + "/events";
}

std::string topic_raw_frames(const std::string& prefix, const std::string& node) {
    return prefix + "/" + node + "/rf/raw";
}

}  // namespace mqtt_service
