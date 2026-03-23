#include "wmbus_minimal_pipeline/wmbus_pipeline.hpp"
#include <cstring>

namespace wmbus_minimal_pipeline {

// WmbusFrame field accessors — safe extraction from raw hex
// These depend on the hex string being present and valid.
// For short frames, return 0.

uint8_t WmbusFrame::l_field() const {
    if (raw_hex.size() < 2) return 0;
    unsigned val = 0;
    if (std::sscanf(raw_hex.c_str(), "%2x", &val) == 1) {
        return static_cast<uint8_t>(val);
    }
    return 0;
}

uint8_t WmbusFrame::c_field() const {
    if (raw_hex.size() < 4) return 0;
    unsigned val = 0;
    if (std::sscanf(raw_hex.c_str() + 2, "%2x", &val) == 1) {
        return static_cast<uint8_t>(val);
    }
    return 0;
}

uint16_t WmbusFrame::manufacturer_id() const {
    // Bytes 3-4 (hex positions 4-7), little-endian
    if (raw_hex.size() < 8) return 0;
    unsigned b3 = 0, b4 = 0;
    std::sscanf(raw_hex.c_str() + 4, "%2x", &b3);
    std::sscanf(raw_hex.c_str() + 6, "%2x", &b4);
    return static_cast<uint16_t>((b4 << 8) | b3);
}

uint32_t WmbusFrame::device_id() const {
    // Bytes 5-8 (hex positions 8-15), BCD little-endian
    if (raw_hex.size() < 16) return 0;
    unsigned b5 = 0, b6 = 0, b7 = 0, b8 = 0;
    std::sscanf(raw_hex.c_str() + 8, "%2x", &b5);
    std::sscanf(raw_hex.c_str() + 10, "%2x", &b6);
    std::sscanf(raw_hex.c_str() + 12, "%2x", &b7);
    std::sscanf(raw_hex.c_str() + 14, "%2x", &b8);
    return (b8 << 24) | (b7 << 16) | (b6 << 8) | b5;
}

common::Result<WmbusFrame> WmbusPipeline::from_radio_frame(
    const radio_cc1101::RawRadioFrame& raw,
    int64_t timestamp_ms,
    uint32_t rx_count) {

    if (raw.length == 0) {
        return common::Result<WmbusFrame>::error(
            common::ErrorCode::InvalidArgument);
    }

    WmbusFrame frame;
    frame.raw_hex = bytes_to_hex(raw.data, raw.length);

    frame.metadata.rssi_dbm = raw.rssi_dbm;
    frame.metadata.lqi = raw.lqi;
    frame.metadata.crc_ok = raw.crc_ok;
    frame.metadata.frame_length = raw.length;
    frame.metadata.timestamp_ms = timestamp_ms;
    frame.metadata.rx_count = rx_count;

    return common::Result<WmbusFrame>::ok(std::move(frame));
}

std::string WmbusPipeline::bytes_to_hex(const uint8_t* data, size_t length) {
    static constexpr char hex_chars[] = "0123456789ABCDEF";

    std::string result;
    result.reserve(length * 2);

    for (size_t i = 0; i < length; ++i) {
        result += hex_chars[(data[i] >> 4) & 0x0F];
        result += hex_chars[data[i] & 0x0F];
    }
    return result;
}

size_t WmbusPipeline::hex_to_bytes(const char* hex, uint8_t* out,
                                    size_t out_size) {
    if (!hex || !out) return 0;

    size_t hex_len = std::strlen(hex);
    size_t byte_count = hex_len / 2;
    if (byte_count > out_size) byte_count = out_size;

    auto hex_nibble = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
        if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
        if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
        return 0;
    };

    for (size_t i = 0; i < byte_count; ++i) {
        out[i] = static_cast<uint8_t>((hex_nibble(hex[i * 2]) << 4) |
                                       hex_nibble(hex[i * 2 + 1]));
    }
    return byte_count;
}

} // namespace wmbus_minimal_pipeline
