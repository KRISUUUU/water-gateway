#pragma once

#include <cstdint>
#include <string>

namespace wmbus_minimal_pipeline {

struct WmbusFrameMetadata {
    int8_t rssi_dbm = 0;
    uint8_t lqi = 0;
    bool crc_ok = false;
    uint16_t frame_length = 0;
    int64_t timestamp_ms = 0; // Epoch ms (0 if NTP not synced)
    uint32_t rx_count = 0;    // Monotonic reception counter
};

struct WmbusFrame {
    std::string raw_hex; // Uppercase hex representation of raw bytes
    WmbusFrameMetadata metadata;

    // Basic WMBus T-mode L-field (first byte = length of remaining data)
    uint8_t l_field() const;

    // Basic WMBus C-field (second byte, frame type/direction)
    uint8_t c_field() const;

    // Manufacturer ID (bytes 3-4, little-endian encoded)
    uint16_t manufacturer_id() const;

    // Device serial (bytes 5-8, BCD-encoded)
    uint32_t device_id() const;
};

} // namespace wmbus_minimal_pipeline
