#pragma once

#include "radio_cc1101/radio_cc1101.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace wmbus_minimal_pipeline {

struct WmbusFrameMetadata {
    int8_t rssi_dbm = 0;
    uint8_t lqi = 0;
    bool crc_ok = false;
    bool radio_crc_available = false;
    bool raw_frame_contract_valid = false;
    radio_cc1101::RadioBurstEndReason burst_end_reason = radio_cc1101::RadioBurstEndReason::None;
    uint8_t first_data_byte = 0;
    uint16_t payload_offset = 0;
    uint16_t payload_length = 0;
    uint16_t captured_frame_length = 0;
    uint16_t canonical_frame_length = 0;
    int64_t timestamp_ms = 0; // Epoch ms (0 if NTP not synced)
    uint32_t rx_count = 0;    // Monotonic reception counter
};

struct WmbusFrame {
    std::vector<uint8_t> raw_bytes; // Canonical pipeline bytes: decoded WMBus or encoded payload
    std::vector<uint8_t> original_raw_bytes; // Exact radio-layer bytes before pipeline decode
    bool decoded_ok = false; // true only when 3-of-6 decode succeeded and the decoded frame validated
    WmbusFrameMetadata metadata;

    // Presentation helpers for API/UI/logging.
    std::string canonical_hex() const;
    std::string captured_hex() const;

    // Basic WMBus T-mode L-field (first byte = length of remaining data)
    uint8_t l_field() const;

    // Basic WMBus C-field (second byte, frame type/direction)
    uint8_t c_field() const;

    // Manufacturer ID (bytes 3-4, little-endian encoded)
    uint16_t manufacturer_id() const;

    // Device serial (bytes 5-8, BCD-encoded)
    uint32_t device_id() const;

    // Conservative identity key for product-layer indexing.
    // Signature fallback is used until a decoded frame exposes a reliable manufacturer/device pair.
    std::string identity_key() const;

    bool has_reliable_identity() const;

    // Best-effort stable key for dedup/indexing without converting to hex.
    std::string dedup_key() const;

    // Hex prefix used for unknown meter signature fallback.
    std::string signature_prefix_hex(size_t max_bytes = 12) const;
};

} // namespace wmbus_minimal_pipeline
