#pragma once

#include <string>

namespace mqtt_service {

std::string make_status_payload(const std::string& state, const std::string& version);
std::string make_event_payload(const std::string& type, const std::string& message);
std::string make_raw_frame_payload(const std::string& raw_hex, int rssi, bool crc_ok);

}  // namespace mqtt_service
