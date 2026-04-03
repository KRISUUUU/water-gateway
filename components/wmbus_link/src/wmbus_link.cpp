#include "wmbus_link/wmbus_link.hpp"
#include "wmbus_tmode_rx/wmbus_block_validation.hpp"

#include <algorithm>
#include <cstdio>

namespace wmbus_link {

namespace {

bool identity_bytes_available(const EncodedRxFrame& frame) {
    return frame.decoded_length >= 8U;
}

} // namespace

std::string LinkTelegram::canonical_hex() const {
    return WmbusLink::bytes_to_hex(canonical_bytes.data(), metadata.canonical_length);
}

std::string LinkTelegram::dedup_key() const {
    return std::string(reinterpret_cast<const char*>(canonical_bytes.data()),
                       reinterpret_cast<const char*>(canonical_bytes.data() + metadata.canonical_length));
}

std::string LinkTelegram::identity_key() const {
    if (reliable_identity) {
        char key[32];
        std::snprintf(key, sizeof(key), "mfg:%04X-id:%08X", static_cast<unsigned int>(manufacturer_id),
                      static_cast<unsigned int>(device_id));
        return key;
    }

    const std::string sig = signature_prefix_hex(12);
    return "sig:" + (sig.empty() ? "EMPTY" : sig);
}

std::string LinkTelegram::signature_prefix_hex(size_t max_bytes) const {
    const size_t n = std::min(max_bytes, static_cast<size_t>(metadata.canonical_length));
    return WmbusLink::bytes_to_hex(canonical_bytes.data(), n);
}

std::string ValidatedTelegram::canonical_hex() const {
    return link.canonical_hex();
}

std::string ValidatedTelegram::captured_hex() const {
    return WmbusLink::bytes_to_hex(exact_frame.encoded_bytes.data(), exact_frame.encoded_length);
}

std::string ValidatedTelegram::dedup_key() const {
    return link.dedup_key();
}

std::string ValidatedTelegram::identity_key() const {
    return link.identity_key();
}

std::string ValidatedTelegram::signature_prefix_hex(size_t max_bytes) const {
    return link.signature_prefix_hex(max_bytes);
}

uint16_t ValidatedTelegram::manufacturer_id() const {
    return link.manufacturer_id;
}

uint32_t ValidatedTelegram::device_id() const {
    return link.device_id;
}

bool ValidatedTelegram::has_reliable_identity() const {
    return link.reliable_identity;
}

std::string WmbusLink::bytes_to_hex(const uint8_t* data, size_t length) {
    if (!data || length == 0) {
        return {};
    }

    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(length * 2U);
    for (size_t i = 0; i < length; ++i) {
        result.push_back(kHex[(data[i] >> 4U) & 0x0FU]);
        result.push_back(kHex[data[i] & 0x0FU]);
    }
    return result;
}

ExactFrameValidationResult WmbusLink::validate_exact_frame(const EncodedRxFrame& frame) {
    ExactFrameValidationResult result{};

    if (frame.orientation == wmbus_tmode_rx::FrameOrientation::Unknown) {
        result.reject_reason = ExactFrameRejectReason::InvalidOrientation;
        return result;
    }
    if (frame.decoded_length == 0U || frame.encoded_length == 0U ||
        frame.decoded_length > EncodedRxFrame::kMaxDecodedBytes ||
        frame.encoded_length > EncodedRxFrame::kMaxEncodedBytes) {
        result.reject_reason = ExactFrameRejectReason::InvalidLength;
        return result;
    }
    if (frame.decoded_length != wmbus_tmode_rx::calculate_format_a_decoded_length(frame.l_field)) {
        result.reject_reason = ExactFrameRejectReason::DecodedLengthMismatch;
        return result;
    }
    if (frame.encoded_length != frame.exact_encoded_bytes_required) {
        result.reject_reason = ExactFrameRejectReason::ExactLengthMismatch;
        return result;
    }
    if (frame.first_block_validation != wmbus_tmode_rx::FirstBlockValidationState::Passed) {
        result.reject_reason = ExactFrameRejectReason::InvalidFirstBlock;
        return result;
    }

    result.accepted = true;
    return result;
}

LinkValidationResult WmbusLink::validate_and_build(const EncodedRxFrame& frame) {
    LinkValidationResult result{};
    const auto exact_validation = validate_exact_frame(frame);
    if (!exact_validation.accepted) {
        switch (exact_validation.reject_reason) {
        case ExactFrameRejectReason::InvalidLength:
            result.reject_reason = LinkRejectReason::InvalidLength;
            break;
        case ExactFrameRejectReason::DecodedLengthMismatch:
            result.reject_reason = LinkRejectReason::DecodedLengthMismatch;
            break;
        case ExactFrameRejectReason::ExactLengthMismatch:
            result.reject_reason = LinkRejectReason::ExactLengthMismatch;
            break;
        case ExactFrameRejectReason::InvalidOrientation:
            result.reject_reason = LinkRejectReason::InvalidOrientation;
            break;
        case ExactFrameRejectReason::InvalidFirstBlock:
            result.reject_reason = LinkRejectReason::FirstBlockValidationFailed;
            break;
        case ExactFrameRejectReason::None:
        default:
            result.reject_reason = LinkRejectReason::None;
            break;
        }
        return result;
    }

    if (frame.decoded_length < 12U) {
        result.reject_reason = LinkRejectReason::FrameTooShort;
        return result;
    }

    const auto first_block = wmbus_tmode_rx::validate_first_block(frame.decoded_bytes.data(), frame.decoded_length);
    if (first_block.state != wmbus_tmode_rx::FirstBlockValidationState::Passed) {
        result.reject_reason = LinkRejectReason::FirstBlockValidationFailed;
        return result;
    }

    size_t offset = 12U;
    while (offset < frame.decoded_length) {
        const size_t remaining = static_cast<size_t>(frame.decoded_length) - offset;
        if (remaining <= 2U) {
            result.reject_reason = LinkRejectReason::BlockValidationFailed;
            return result;
        }
        const size_t block_data_length = remaining > 18U ? 16U : (remaining - 2U);
        const uint16_t expected_crc =
            wmbus_tmode_rx::calculate_wmbus_crc16(frame.decoded_bytes.data() + offset, block_data_length);
        const uint16_t observed_crc =
            static_cast<uint16_t>((static_cast<uint16_t>(frame.decoded_bytes[offset + block_data_length]) << 8U) |
                                  static_cast<uint16_t>(frame.decoded_bytes[offset + block_data_length + 1U]));
        if (expected_crc != observed_crc) {
            result.reject_reason = LinkRejectReason::BlockValidationFailed;
            return result;
        }
        offset += block_data_length + 2U;
    }

    LinkTelegram link{};
    for (uint16_t i = 0; i < frame.decoded_length; ++i) {
        link.canonical_bytes[i] = frame.decoded_bytes[i];
    }
    link.metadata.rssi_dbm = frame.metadata.rssi_dbm;
    link.metadata.lqi = frame.metadata.lqi;
    link.metadata.crc_ok = frame.metadata.crc_ok;
    link.metadata.radio_crc_available = frame.metadata.radio_crc_available;
    link.metadata.exact_frame_contract_valid = frame.metadata.exact_frame_contract_valid;
    link.metadata.transitional_raw_adapter_used = frame.metadata.transitional_raw_adapter_used;
    link.metadata.timestamp_ms = frame.metadata.timestamp_ms;
    link.metadata.rx_count = frame.metadata.rx_count;
    link.metadata.encoded_length = frame.encoded_length;
    link.metadata.exact_encoded_bytes_required = frame.exact_encoded_bytes_required;
    link.metadata.canonical_length = frame.decoded_length;
    link.metadata.orientation = frame.orientation;

    if (identity_bytes_available(frame)) {
        const uint16_t mfg = static_cast<uint16_t>(
            (static_cast<uint16_t>(frame.decoded_bytes[3]) << 8U) | frame.decoded_bytes[2]);
        const uint32_t dev = (static_cast<uint32_t>(frame.decoded_bytes[7]) << 24U) |
                             (static_cast<uint32_t>(frame.decoded_bytes[6]) << 16U) |
                             (static_cast<uint32_t>(frame.decoded_bytes[5]) << 8U) |
                             static_cast<uint32_t>(frame.decoded_bytes[4]);
        link.manufacturer_id = mfg;
        link.device_id = dev;
        link.reliable_identity = (mfg != 0U || dev != 0U);
    } else {
        result.reject_reason = LinkRejectReason::IdentityUnavailable;
        return result;
    }

    result.accepted = true;
    result.telegram.exact_frame = frame;
    result.telegram.link = link;
    return result;
}

// Map each LinkRejectReason to the most specific rf_diagnostics::RejectReason.
// Keep this in sync with the LinkRejectReason enum whenever new reasons are added.
rf_diagnostics::RejectReason link_reject_to_rf_reason(LinkRejectReason reason) {
    switch (reason) {
    case LinkRejectReason::InvalidLength:
        return rf_diagnostics::RejectReason::InvalidLength;
    case LinkRejectReason::InvalidOrientation:
        return rf_diagnostics::RejectReason::InvalidOrientation;
    case LinkRejectReason::FrameTooShort:
        return rf_diagnostics::RejectReason::FrameTooShort;
    case LinkRejectReason::DecodedLengthMismatch:
        return rf_diagnostics::RejectReason::DecodedLengthMismatch;
    case LinkRejectReason::ExactLengthMismatch:
        return rf_diagnostics::RejectReason::ExactLengthMismatch;
    case LinkRejectReason::FirstBlockValidationFailed:
        return rf_diagnostics::RejectReason::FirstBlockValidationFailed;
    case LinkRejectReason::BlockValidationFailed:
        return rf_diagnostics::RejectReason::BlockValidationFailed;
    case LinkRejectReason::IdentityUnavailable:
        return rf_diagnostics::RejectReason::IdentityUnavailable;
    case LinkRejectReason::None:
    default:
        return rf_diagnostics::RejectReason::Unknown;
    }
}

} // namespace wmbus_link
