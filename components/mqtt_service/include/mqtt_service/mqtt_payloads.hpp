#pragma once

#include <cstdint>
#include <string>

namespace mqtt_service {

// All payload builders produce JSON strings.
// These are pure functions for host-testability.

// Status payload published on connect and periodically
std::string payload_status_online(const char* firmware_version,
                                   const char* ip_address,
                                   const char* hostname,
                                   uint32_t uptime_s,
                                   const char* health_state);

std::string payload_status_offline();

// Telemetry payload published periodically
std::string payload_telemetry(uint32_t uptime_s,
                               uint32_t free_heap_bytes,
                               uint32_t min_free_heap_bytes,
                               int8_t wifi_rssi_dbm,
                               const char* mqtt_state,
                               const char* radio_state,
                               uint32_t frames_received,
                               uint32_t frames_published,
                               uint32_t frames_duplicate,
                               uint32_t frames_crc_fail,
                               uint32_t mqtt_publishes,
                               uint32_t mqtt_failures,
                               const char* timestamp);

// Event payload for discrete events
std::string payload_event(const char* event_type,
                           const char* severity,
                           const char* message,
                           const char* timestamp);

// Raw frame payload for received WMBus telegrams
std::string payload_raw_frame(const char* raw_hex,
                               uint16_t frame_length,
                               int8_t rssi_dbm,
                               uint8_t lqi,
                               bool crc_ok,
                               uint16_t manufacturer_id,
                               uint32_t device_id,
                               const char* meter_key,
                               const char* timestamp,
                               uint32_t rx_count);

} // namespace mqtt_service
