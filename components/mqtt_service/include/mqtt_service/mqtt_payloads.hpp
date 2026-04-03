#pragma once

#include <cstdint>
#include <string>

namespace mqtt_service {

// All payload builders produce JSON strings.
// These are pure functions for host-testability.

// Status payload published on connect and periodically
std::string payload_status_online(const char* firmware_version, const char* ip_address,
                                  const char* hostname, uint32_t uptime_s,
                                  const char* health_state);

std::string payload_status_offline();

} // namespace mqtt_service
