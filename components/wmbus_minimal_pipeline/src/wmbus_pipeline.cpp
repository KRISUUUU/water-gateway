#include "wmbus_minimal_pipeline/wmbus_pipeline.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace wmbus_minimal_pipeline {

namespace {
static const uint8_t kDecode3of6High[64] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x30, 0xFF, 0x10, 0x20,
    0xFF, 0xFF, 0xFF, 0xFF, 0x70, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0x50, 0x60, 0xFF, 0x40, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xB0, 0xFF, 0x90, 0xA0, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0x80,
    0xFF, 0xFF, 0xFF, 0xFF, 0xD0, 0xE0, 0xFF, 0xC0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
};

static const uint8_t kDecode3of6Low[64] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0xFF, 0x01, 0x02,
    0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0x05, 0x06, 0xFF, 0x04, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0B, 0xFF, 0x09, 0x0A, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF, 0x08,
    0xFF, 0xFF, 0xFF, 0xFF, 0x0D, 0x0E, 0xFF, 0x0C, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
};

uint8_t byte_at(const std::vector<uint8_t>& bytes, size_t idx) {
    if (idx >= bytes.size()) {
        return 0;
    }
    return bytes[idx];
}

bool raw_frame_contract_is_valid(const radio_cc1101::RawRadioFrame& raw) {
    if (raw.length == 0 || raw.length > radio_cc1101::RawRadioFrame::MAX_DATA_SIZE) {
        return false;
    }
    if (raw.payload_offset != 0U) {
        return false;
    }
    if (raw.payload_length == 0U || raw.payload_length != raw.length) {
        return false;
    }
    if (raw.first_data_byte != raw.data[0]) {
        return false;
    }
    return true;
}

uint8_t reverse_bits8(uint8_t value) {
    return static_cast<uint8_t>(((value & 0x01U) << 7U) | ((value & 0x02U) << 5U) |
                                ((value & 0x04U) << 3U) | ((value & 0x08U) << 1U) |
                                ((value & 0x10U) >> 1U) | ((value & 0x20U) >> 3U) |
                                ((value & 0x40U) >> 5U) | ((value & 0x80U) >> 7U));
}

bool decode_3of6_bytes(const uint8_t* raw, size_t raw_len, std::vector<uint8_t>& out) {
    out.clear();
    if (!raw || raw_len == 0) {
        return false;
    }

    const size_t total_bits = raw_len * 8U;
    auto read_bits = [raw, total_bits](size_t bit_pos, size_t bit_count) -> uint8_t {
        if (bit_pos + bit_count > total_bits || bit_count == 0 || bit_count > 8U) {
            return 0;
        }

        uint8_t value = 0;
        for (size_t i = 0; i < bit_count; ++i) {
            const size_t absolute_bit = bit_pos + i;
            const size_t byte_index = absolute_bit / 8U;
            const uint8_t bit_index = static_cast<uint8_t>(7U - (absolute_bit % 8U));
            value = static_cast<uint8_t>((value << 1U) | ((raw[byte_index] >> bit_index) & 0x01U));
        }
        return value;
    };

    for (size_t bit_pos = 0; bit_pos + 12U <= total_bits; bit_pos += 12U) {
        const uint8_t sym1 = read_bits(bit_pos, 6U);
        const uint8_t sym2 = read_bits(bit_pos + 6U, 6U);
        const uint8_t hi = kDecode3of6High[sym1];
        const uint8_t lo = kDecode3of6Low[sym2];

        if (hi == 0xFF || lo == 0xFF) {
            out.clear();
            return false;
        }

        out.push_back(static_cast<uint8_t>(hi | lo));
    }

    if (out.empty()) {
        return false;
    }

    const size_t trailing_bits = total_bits % 12U;
    if (trailing_bits != 0U && read_bits(total_bits - trailing_bits, trailing_bits) != 0U) {
        out.clear();
        return false;
    }
    return true;
}

bool validate_decoded_frame(const std::vector<uint8_t>& decoded) {
    if (decoded.size() < 10U) {
        return false;
    }

    const size_t expected_size = static_cast<size_t>(decoded[0]) + 1U;
    if (expected_size != decoded.size()) {
        return false;
    }

    return true;
}
} // namespace

std::string WmbusFrame::canonical_hex() const {
    return WmbusPipeline::bytes_to_hex(raw_bytes);
}

std::string WmbusFrame::captured_hex() const {
    return WmbusPipeline::bytes_to_hex(original_raw_bytes);
}

uint8_t WmbusFrame::l_field() const {
    return decoded_ok ? byte_at(raw_bytes, 0) : 0;
}

uint8_t WmbusFrame::c_field() const {
    return decoded_ok ? byte_at(raw_bytes, 1) : 0;
}

uint16_t WmbusFrame::manufacturer_id() const {
    if (!has_reliable_identity()) {
        return 0;
    }
    const uint16_t b3 = byte_at(raw_bytes, 2);
    const uint16_t b4 = byte_at(raw_bytes, 3);
    return static_cast<uint16_t>((b4 << 8U) | b3);
}

uint32_t WmbusFrame::device_id() const {
    if (!has_reliable_identity()) {
        return 0;
    }
    const uint32_t b5 = byte_at(raw_bytes, 4);
    const uint32_t b6 = byte_at(raw_bytes, 5);
    const uint32_t b7 = byte_at(raw_bytes, 6);
    const uint32_t b8 = byte_at(raw_bytes, 7);
    return (b8 << 24U) | (b7 << 16U) | (b6 << 8U) | b5;
}

bool WmbusFrame::has_reliable_identity() const {
    if (!decoded_ok || raw_bytes.size() < 10U) {
        return false;
    }
    const uint16_t mfg =
        static_cast<uint16_t>((static_cast<uint16_t>(byte_at(raw_bytes, 3)) << 8U) | byte_at(raw_bytes, 2));
    const uint32_t dev = (static_cast<uint32_t>(byte_at(raw_bytes, 7)) << 24U) |
                         (static_cast<uint32_t>(byte_at(raw_bytes, 6)) << 16U) |
                         (static_cast<uint32_t>(byte_at(raw_bytes, 5)) << 8U) |
                         static_cast<uint32_t>(byte_at(raw_bytes, 4));
    return mfg != 0U || dev != 0U;
}

std::string WmbusFrame::identity_key() const {
    if (has_reliable_identity()) {
        const uint16_t mfg = manufacturer_id();
        const uint32_t dev = device_id();
        if (mfg != 0U || dev != 0U) {
            char key[32];
            std::snprintf(key, sizeof(key), "mfg:%04X-id:%08X", static_cast<unsigned int>(mfg),
                          static_cast<unsigned int>(dev));
            return key;
        }
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
    if (!raw_frame_contract_is_valid(raw)) {
        return common::Result<WmbusFrame>::error(common::ErrorCode::InvalidArgument);
    }

    WmbusFrame frame;
    frame.raw_bytes.assign(raw.data, raw.data + raw.length);
    frame.original_raw_bytes = frame.raw_bytes;

    std::vector<uint8_t> decoded;
    bool decode_ok = decode_3of6_bytes(frame.raw_bytes.data(), frame.raw_bytes.size(), decoded);
    if ((!decode_ok || decoded.empty()) && !frame.raw_bytes.empty()) {
        std::vector<uint8_t> reversed(frame.raw_bytes);
        for (auto& b : reversed) {
            b = reverse_bits8(b);
        }
        decode_ok = decode_3of6_bytes(reversed.data(), reversed.size(), decoded);
    }
    if (decode_ok && validate_decoded_frame(decoded)) {
        frame.raw_bytes = std::move(decoded);
        frame.decoded_ok = true;
    }

    frame.metadata.rssi_dbm = raw.rssi_dbm;
    frame.metadata.lqi = raw.lqi;
    frame.metadata.crc_ok = raw.crc_ok;
    frame.metadata.radio_crc_available = raw.radio_crc_available;
    frame.metadata.raw_frame_contract_valid = true;
    frame.metadata.burst_end_reason = raw.burst_end_reason;
    frame.metadata.first_data_byte = raw.first_data_byte;
    frame.metadata.payload_offset = raw.payload_offset;
    frame.metadata.payload_length = raw.payload_length;
    frame.metadata.captured_frame_length = static_cast<uint16_t>(frame.original_raw_bytes.size());
    frame.metadata.canonical_frame_length = static_cast<uint16_t>(frame.raw_bytes.size());
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
