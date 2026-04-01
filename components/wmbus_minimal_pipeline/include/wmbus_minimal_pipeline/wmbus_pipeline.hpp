#pragma once

#include "common/result.hpp"
#include "radio_cc1101/radio_cc1101.hpp"
#include "wmbus_minimal_pipeline/wmbus_frame.hpp"
#include <string>
#include <vector>

namespace wmbus_minimal_pipeline {

class WmbusPipeline {
  public:
    // Convert a radio-layer packet into a WmbusFrame with metadata.
    // Expected RawRadioFrame contract:
    // - original bytes are the exact data bytes drained from one CC1101 RX burst
    // - there is no packet-length-prefix semantics in this contract
    // - payload bytes begin at payload_offset and are the only bytes considered for 3-of-6 decode
    // This is the primary pipeline entry point.
    // Pure function — no side effects, fully host-testable.
    static common::Result<WmbusFrame> from_radio_frame(const radio_cc1101::RawRadioFrame& raw,
                                                       int64_t timestamp_ms, uint32_t rx_count);

    // Convert raw bytes to uppercase hex string.
    // Pure function, host-testable.
    static std::string bytes_to_hex(const uint8_t* data, size_t length);
    static std::string bytes_to_hex(const std::vector<uint8_t>& data);

    // Parse hex string back to bytes. Returns number of bytes written.
    static size_t hex_to_bytes(const char* hex, uint8_t* out, size_t out_size);
};

} // namespace wmbus_minimal_pipeline
