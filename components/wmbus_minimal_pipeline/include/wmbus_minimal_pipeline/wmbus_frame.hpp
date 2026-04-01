#pragma once

#include <cstdint>
#include <string>
#include <vector>

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
    std::vector<uint8_t> raw_bytes; // Canonical raw frame bytes
    std::vector<uint8_t> original_raw_bytes; // Raw captured bytes before 3-of-6 decode
    bool decoded_ok = false;
    WmbusFrameMetadata metadata;

    // Presentation helper for API/UI/logging.
    std::string raw_hex() const;
    std::string original_raw_hex() const;

    // Basic WMBus T-mode L-field (first byte = length of remaining data)
    uint8_t l_field() const;

    // Basic WMBus C-field (second byte, frame type/direction)
    uint8_t c_field() const;

    // Manufacturer ID (bytes 3-4, little-endian encoded)
    uint16_t manufacturer_id() const;

    // Device serial (bytes 5-8, BCD-encoded)
    uint32_t device_id() const;

    // Conservative identity key for product-layer indexing.
    // Raw T-mode capture uses signature fallback until 3-of-6 decode exists.
    std::string identity_key() const;

    // Best-effort stable key for dedup/indexing without converting to hex.
    std::string dedup_key() const;

    // Hex prefix used for unknown meter signature fallback.
    std::string signature_prefix_hex(size_t max_bytes = 12) const;
};

} // namespace wmbus_minimal_pipeline
