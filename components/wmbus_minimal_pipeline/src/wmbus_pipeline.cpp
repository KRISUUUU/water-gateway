#include "wmbus_minimal_pipeline/wmbus_pipeline.hpp"
#include "wmbus_link/wmbus_link.hpp"
#include "wmbus_tmode_rx/wmbus_tmode_framer.hpp"
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

bool transitional_raw_to_exact_frame(const radio_cc1101::RawRadioFrame& raw,
                                     wmbus_link::EncodedRxFrame& exact_frame) {
    // Transitional adapter only: this bridges the current polling raw-burst path into the new
    // exact-frame and link-layer contracts until the IRQ/session RX path becomes the sole producer.
    wmbus_tmode_rx::WmbusTmodeFramer framer;
    wmbus_tmode_rx::FeedResult feed_result{};
    for (uint16_t i = 0; i < raw.length; ++i) {
        feed_result = framer.feed_byte(raw.data[i]);
        if (feed_result.state == wmbus_tmode_rx::FramerState::CandidateRejected) {
            return false;
        }
    }

    if (!feed_result.has_complete_frame ||
        feed_result.state != wmbus_tmode_rx::FramerState::ExactFrameComplete) {
        return false;
    }

    const auto& frame = feed_result.frame;
    exact_frame.encoded_length = frame.encoded_length;
    exact_frame.decoded_length = frame.decoded_length;
    exact_frame.exact_encoded_bytes_required = frame.exact_encoded_bytes_required;
    exact_frame.l_field = frame.l_field;
    exact_frame.orientation = frame.orientation;
    exact_frame.first_block_validation = frame.first_block_validation;
    for (uint16_t i = 0; i < frame.encoded_length; ++i) {
        exact_frame.encoded_bytes[i] = frame.encoded_bytes[i];
    }
    for (uint16_t i = 0; i < frame.decoded_length; ++i) {
        exact_frame.decoded_bytes[i] = frame.decoded_bytes[i];
    }
    exact_frame.metadata.rssi_dbm = raw.rssi_dbm;
    exact_frame.metadata.lqi = raw.lqi;
    exact_frame.metadata.crc_ok = raw.crc_ok;
    exact_frame.metadata.radio_crc_available = raw.radio_crc_available;
    exact_frame.metadata.exact_frame_contract_valid = true;
    exact_frame.metadata.transitional_raw_adapter_used = true;
    exact_frame.metadata.capture_elapsed_ms = raw.capture_elapsed_ms;
    exact_frame.metadata.captured_frame_length = raw.length;
    exact_frame.metadata.first_data_byte = raw.first_data_byte;
    exact_frame.metadata.burst_end_reason = static_cast<uint8_t>(raw.burst_end_reason);
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

    wmbus_link::EncodedRxFrame exact_frame{};
    if (transitional_raw_to_exact_frame(raw, exact_frame)) {
        exact_frame.metadata.timestamp_ms = timestamp_ms;
        exact_frame.metadata.rx_count = rx_count;
        const auto link_result = wmbus_link::WmbusLink::validate_and_build(exact_frame);
        if (link_result.accepted) {
            frame.raw_bytes.assign(link_result.telegram.link.canonical_bytes.begin(),
                                   link_result.telegram.link.canonical_bytes.begin() +
                                       link_result.telegram.link.metadata.canonical_length);
            frame.decoded_ok = true;
        }
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
    frame.metadata.capture_elapsed_ms = raw.capture_elapsed_ms;
    frame.metadata.captured_frame_length = static_cast<uint16_t>(frame.original_raw_bytes.size());
    frame.metadata.canonical_frame_length = static_cast<uint16_t>(frame.raw_bytes.size());
    frame.metadata.timestamp_ms = timestamp_ms;
    frame.metadata.rx_count = rx_count;

    return common::Result<WmbusFrame>::ok(std::move(frame));
}

common::Result<WmbusFrame> WmbusPipeline::from_exact_frame(
    const wmbus_link::EncodedRxFrame& exact_frame) {
    const auto link_result = wmbus_link::WmbusLink::validate_and_build(exact_frame);
    if (!link_result.accepted) {
        return common::Result<WmbusFrame>::error(common::ErrorCode::ValidationFailed);
    }

    WmbusFrame frame;
    frame.raw_bytes.assign(link_result.telegram.link.canonical_bytes.begin(),
                           link_result.telegram.link.canonical_bytes.begin() +
                               link_result.telegram.link.metadata.canonical_length);
    frame.original_raw_bytes.assign(exact_frame.encoded_bytes.begin(),
                                    exact_frame.encoded_bytes.begin() + exact_frame.encoded_length);
    frame.decoded_ok = true;
    frame.metadata.rssi_dbm = exact_frame.metadata.rssi_dbm;
    frame.metadata.lqi = exact_frame.metadata.lqi;
    frame.metadata.crc_ok = exact_frame.metadata.crc_ok;
    frame.metadata.radio_crc_available = exact_frame.metadata.radio_crc_available;
    frame.metadata.raw_frame_contract_valid = exact_frame.metadata.exact_frame_contract_valid;
    frame.metadata.burst_end_reason =
        static_cast<radio_cc1101::RadioBurstEndReason>(exact_frame.metadata.burst_end_reason);
    frame.metadata.first_data_byte = exact_frame.metadata.first_data_byte;
    frame.metadata.payload_offset = 0;
    frame.metadata.payload_length = exact_frame.encoded_length;
    frame.metadata.capture_elapsed_ms = exact_frame.metadata.capture_elapsed_ms;
    frame.metadata.captured_frame_length = exact_frame.metadata.captured_frame_length;
    frame.metadata.canonical_frame_length =
        static_cast<uint16_t>(link_result.telegram.link.metadata.canonical_length);
    frame.metadata.timestamp_ms = exact_frame.metadata.timestamp_ms;
    frame.metadata.rx_count = exact_frame.metadata.rx_count;
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
