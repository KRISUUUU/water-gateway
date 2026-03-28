#include "wmbus_minimal_pipeline/wmbus_pipeline.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace wmbus_minimal_pipeline {

namespace {

// EN 13757-4 Mode-T 3-of-6: map 6-bit codeword (0..63) to high nibble 0xN0 or 0xFF if invalid.
// Table from rtl_433 src/devices/m_bus.c (Wireless M-Bus EN 13757-4).
uint8_t decode_3of6_symbol(uint8_t six_bits) {
    const uint8_t b = static_cast<uint8_t>(six_bits & 0x3FU);
    switch (b) {
    case 22:
        return 0x00U;
    case 13:
        return 0x01U;
    case 14:
        return 0x02U;
    case 11:
        return 0x03U;
    case 28:
        return 0x04U;
    case 25:
        return 0x05U;
    case 26:
        return 0x06U;
    case 19:
        return 0x07U;
    case 44:
        return 0x08U;
    case 37:
        return 0x09U;
    case 38:
        return 0x0AU;
    case 35:
        return 0x0BU;
    case 52:
        return 0x0CU;
    case 49:
        return 0x0DU;
    case 50:
        return 0x0EU;
    case 41:
        return 0x0FU;
    default:
        return 0xFFU;
    }
}

// rtl_433 bit_util.c crc16() with polynomial 0x3D65, init 0; Wireless M-Bus compares ~remainder.
uint16_t crc16_en13757_block(const uint8_t* message, size_t n_bytes) {
    uint16_t remainder = 0;
    for (size_t byte = 0; byte < n_bytes; ++byte) {
        remainder ^= static_cast<uint16_t>(message[byte]) << 8U;
        for (unsigned bit = 0; bit < 8; ++bit) {
            if (remainder & 0x8000U) {
                remainder = static_cast<uint16_t>((remainder << 1) ^ 0x3D65U);
            } else {
                remainder = static_cast<uint16_t>(remainder << 1);
            }
        }
    }
    return static_cast<uint16_t>(~remainder);
}

// CRC stored big-endian at message[n_bytes] (MSB) and message[n_bytes+1] (LSB).
bool crc_match_at(const uint8_t* message, size_t n_bytes) {
    const uint16_t calc = crc16_en13757_block(message, n_bytes);
    const uint16_t recv =
        (static_cast<uint16_t>(message[n_bytes]) << 8U) | static_cast<uint16_t>(message[n_bytes + 1]);
    return calc == recv;
}

bool verify_dll_crc_chain(const uint8_t* d, size_t len) {
    if (len < 12U) {
        return false;
    }
    if (!crc_match_at(d, 10U)) {
        return false;
    }

    size_t pos = 12U;
    while (pos < len) {
        const size_t rem = len - pos;
        if (rem < 2U) {
            return false;
        }
        if (rem >= 18U) {
            if (!crc_match_at(d + pos, 16U)) {
                return false;
            }
            pos += 18U;
        } else {
            const size_t dlen = rem - 2U;
            if (dlen == 0U || dlen > 16U) {
                return false;
            }
            if (!crc_match_at(d + pos, dlen)) {
                return false;
            }
            pos += rem;
        }
    }
    return true;
}

size_t expected_format_a_decoded_size(size_t data_total) {
    size_t expected = 0;

    const size_t block1_data = std::min<size_t>(data_total, 10U);
    expected += block1_data + 2U;

    size_t remaining = data_total > 10U ? (data_total - 10U) : 0U;
    while (remaining > 0U) {
        const size_t block_data = std::min<size_t>(remaining, 16U);
        expected += block_data + 2U;
        remaining -= block_data;
    }

    return expected;
}

std::vector<uint8_t> strip_format_a_crc_bytes(const std::vector<uint8_t>& decoded,
                                              size_t data_total) {
    std::vector<uint8_t> clean;
    clean.reserve(data_total);

    size_t pos = 0;
    const size_t block1_data = std::min<size_t>(data_total, 10U);
    clean.insert(clean.end(), decoded.begin() + static_cast<std::ptrdiff_t>(pos),
                 decoded.begin() + static_cast<std::ptrdiff_t>(pos + block1_data));
    pos += block1_data + 2U;

    size_t remaining = data_total > 10U ? (data_total - 10U) : 0U;
    while (remaining > 0U) {
        const size_t block_data = std::min<size_t>(remaining, 16U);
        clean.insert(clean.end(), decoded.begin() + static_cast<std::ptrdiff_t>(pos),
                     decoded.begin() + static_cast<std::ptrdiff_t>(pos + block_data));
        pos += block_data + 2U;
        remaining -= block_data;
    }

    return clean;
}

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
    // Bytes 3-4 (index 2-3), little-endian
    const uint16_t b3 = byte_at(raw_bytes, 2);
    const uint16_t b4 = byte_at(raw_bytes, 3);
    return static_cast<uint16_t>((b4 << 8U) | b3);
}

uint32_t WmbusFrame::device_id() const {
    // Bytes 5-8 (index 4-7), little-endian
    const uint32_t b5 = byte_at(raw_bytes, 4);
    const uint32_t b6 = byte_at(raw_bytes, 5);
    const uint32_t b7 = byte_at(raw_bytes, 6);
    const uint32_t b8 = byte_at(raw_bytes, 7);
    return (b8 << 24U) | (b7 << 16U) | (b6 << 8U) | b5;
}

uint8_t WmbusFrame::device_version() const {
    return byte_at(raw_bytes, 8);
}

uint8_t WmbusFrame::device_type() const {
    return byte_at(raw_bytes, 9);
}

std::string WmbusFrame::identity_key() const {
    const uint16_t mfg = manufacturer_id();
    const uint32_t dev = device_id();
    if (mfg != 0 || dev != 0) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "mfg:%04X-id:%08X-t:%02X",
                      static_cast<unsigned int>(mfg), static_cast<unsigned int>(dev),
                      static_cast<unsigned int>(device_type()));
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
    if ((raw.length % 2U) != 0U) {
        return common::Result<WmbusFrame>::error(common::ErrorCode::FormatInvalid);
    }

    const size_t out_len = raw.length / 2U;
    std::vector<uint8_t> decoded;
    decoded.resize(out_len);

    for (size_t i = 0; i < out_len; ++i) {
        const uint8_t hi = decode_3of6_symbol(raw.data[i * 2U]);
        const uint8_t lo = decode_3of6_symbol(raw.data[i * 2U + 1U]);
        if (hi > 0x0FU || lo > 0x0FU) {
            return common::Result<WmbusFrame>::error(common::ErrorCode::FormatInvalid);
        }
        decoded[i] = static_cast<uint8_t>((hi << 4) | lo);
    }

    if (decoded.empty()) {
        return common::Result<WmbusFrame>::error(common::ErrorCode::FormatInvalid);
    }

    const uint8_t L = decoded[0];
    const size_t data_total = static_cast<size_t>(L) + 1U;
    if (decoded.size() != expected_format_a_decoded_size(data_total)) {
        return common::Result<WmbusFrame>::error(common::ErrorCode::FormatInvalid);
    }

    if (!verify_dll_crc_chain(decoded.data(), decoded.size())) {
        return common::Result<WmbusFrame>::error(common::ErrorCode::FormatInvalid);
    }

    WmbusFrame frame;
    frame.raw_bytes = strip_format_a_crc_bytes(decoded, data_total);

    frame.metadata.rssi_dbm = raw.rssi_dbm;
    frame.metadata.lqi = raw.lqi;
    frame.metadata.crc_ok = true;
    frame.metadata.frame_length = static_cast<uint16_t>(data_total);
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
