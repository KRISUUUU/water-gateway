#include "mqtt_service/mqtt_payloads.hpp"

namespace mqtt_service {

std::string make_status_payload(const std::string& state, const std::string& version) {
    return "{\"state\":\"" + state + "\",\"version\":\"" + version + "\"}";
}

std::string make_event_payload(const std::string& type, const std::string& message) {
    return "{\"type\":\"" + type + "\",\"message\":\"" + message + "\"}";
}

std::string make_raw_frame_payload(const std::string& raw_hex, int rssi, bool crc_ok) {
    return "{\"raw_hex\":\"" + raw_hex + "\",\"rssi\":" + std::to_string(rssi) +
           ",\"crc_ok\":" + std::string(crc_ok ? "true" : "false") + "}";
}

}  // namespace mqtt_service
