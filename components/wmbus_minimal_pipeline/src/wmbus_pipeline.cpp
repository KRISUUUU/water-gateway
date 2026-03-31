#include "wmbus_minimal_pipeline/wmbus_pipeline.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace wmbus_minimal_pipeline {

namespace {
uint8_t byte_at(const std::vector<uint8_t>& bytes, size_t idx) {
    if (idx >= bytes.size()) {
        return 0;
    }
    return bytes[idx];
}
} // namespace

std::string WmbusFrame::raw_hex() const {
    return WmbusPipeline::bytes_to_hex(raw_bytes);
}

uint8_t WmbusFrame::l_field() const {
    return byte_at(raw_bytes, 0);
}

uint8_t WmbusFrame::c_field() const {
    return byte_at(raw_bytes, 1);
}

uint16_t WmbusFrame::manufacturer_id() const {
    // NOTE: In T-mode raw capture these bytes are reliable only after 3-of-6 decode.
    // Bytes 3-4 (index 2-3), little-endian
    const uint16_t b3 = byte_at(raw_bytes, 2);
    const uint16_t b4 = byte_at(raw_bytes, 3);
    return static_cast<uint16_t>((b4 << 8U) | b3);
}

uint32_t WmbusFrame::device_id() const {
    // NOTE: In T-mode raw capture these bytes are reliable only after 3-of-6 decode.
    // Bytes 5-8 (index 4-7), little-endian
    const uint32_t b5 = byte_at(raw_bytes, 4);
    const uint32_t b6 = byte_at(raw_bytes, 5);
    const uint32_t b7 = byte_at(raw_bytes, 6);
    const uint32_t b8 = byte_at(raw_bytes, 7);
    return (b8 << 24U) | (b7 << 16U) | (b6 << 8U) | b5;
}

std::string WmbusFrame::identity_key() const {
    const uint16_t mfg = manufacturer_id();
    const uint32_t dev = device_id();
    // In raw T-mode capture, manufacturer/device fields become valid only after 3-of-6 decode.
    if (mfg == 0 && dev == 0) {
        const std::string sig = signature_prefix_hex(12);
        return "sig:" + (sig.empty() ? "EMPTY" : sig);
    }
    if (mfg != 0 || dev != 0) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "mfg:%04X-id:%08X", static_cast<unsigned int>(mfg),
                      static_cast<unsigned int>(dev));
        return std::string(buf);
    }
    const std::string sig = signature_prefix_hex(12);
    return "sig:" + (sig.empty() ? "EMPTY" : sig);
}

std::string WmbusFrame::dedup_key() const {
    if (raw_bytes.empty()) {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(raw_bytes.data()),
                       reinterpret_cast<const char*>(raw_bytes.data() + raw_bytes.size()));
}

std::string WmbusFrame::signature_prefix_hex(size_t max_bytes) const {
    const size_t n = std::min(max_bytes, raw_bytes.size());
    return WmbusPipeline::bytes_to_hex(raw_bytes.data(), n);
}

common::Result<WmbusFrame> WmbusPipeline::from_radio_frame(const radio_cc1101::RawRadioFrame& raw,
                                                           int64_t timestamp_ms,
                                                           uint32_t rx_count) {

    if (raw.length == 0) {
        return common::Result<WmbusFrame>::error(common::ErrorCode::InvalidArgument);
    }
    if (raw.length > radio_cc1101::RawRadioFrame::MAX_DATA_SIZE) {
        return common::Result<WmbusFrame>::error(common::ErrorCode::InvalidArgument);
    }

    WmbusFrame frame;
    frame.raw_bytes.assign(raw.data, raw.data + raw.length);

    frame.metadata.rssi_dbm = raw.rssi_dbm;
    frame.metadata.lqi = raw.lqi;
    frame.metadata.crc_ok = raw.crc_ok;
    frame.metadata.frame_length = raw.length;
    frame.metadata.timestamp_ms = timestamp_ms;
    frame.metadata.rx_count = rx_count;

    return common::Result<WmbusFrame>::ok(std::move(frame));
}

std::string WmbusPipeline::bytes_to_hex(const uint8_t* data, size_t length) {
    if (!data || length == 0) {
        return "";
    }
    static constexpr char hex_chars[] = "0123456789ABCDEF";

    std::string result;
    result.reserve(length * 2);

    for (size_t i = 0; i < length; ++i) {
        result += hex_chars[(data[i] >> 4) & 0x0F];
        result += hex_chars[data[i] & 0x0F];
    }
    return result;
}

std::string WmbusPipeline::bytes_to_hex(const std::vector<uint8_t>& data) {
    return bytes_to_hex(data.data(), data.size());
}

size_t WmbusPipeline::hex_to_bytes(const char* hex, uint8_t* out, size_t out_size) {
    if (!hex || !out)
        return 0;

    size_t hex_len = std::strlen(hex);
    size_t byte_count = hex_len / 2;
    if (byte_count > out_size)
        byte_count = out_size;

    auto hex_nibble = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9')
            return static_cast<uint8_t>(c - '0');
        if (c >= 'A' && c <= 'F')
            return static_cast<uint8_t>(c - 'A' + 10);
        if (c >= 'a' && c <= 'f')
            return static_cast<uint8_t>(c - 'a' + 10);
        return 0;
    };

    for (size_t i = 0; i < byte_count; ++i) {
        out[i] = static_cast<uint8_t>((hex_nibble(hex[i * 2]) << 4) | hex_nibble(hex[i * 2 + 1]));
    }
    return byte_count;
}

} // namespace wmbus_minimal_pipeline
