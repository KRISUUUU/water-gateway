#include "mqtt_service/mqtt_topics.hpp"

namespace mqtt_service {

static std::string build_topic(const std::string& prefix,
                                const std::string& device_id,
                                const char* suffix) {
    return prefix + "/" + device_id + "/" + suffix;
}

std::string topic_status(const std::string& prefix,
                          const std::string& device_id) {
    return build_topic(prefix, device_id, "status");
}

std::string topic_telemetry(const std::string& prefix,
                             const std::string& device_id) {
    return build_topic(prefix, device_id, "telemetry");
}

std::string topic_events(const std::string& prefix,
                          const std::string& device_id) {
    return build_topic(prefix, device_id, "events");
}

std::string topic_raw_frame(const std::string& prefix,
                             const std::string& device_id) {
    return build_topic(prefix, device_id, "rf/raw");
}

} // namespace mqtt_service
