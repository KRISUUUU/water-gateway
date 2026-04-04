#include "wmbus_link/wmbus_tmode_driver.hpp"

#include <algorithm>
#include <cstring>

namespace wmbus_link {

using namespace protocol_driver;

// --- IProtocolDriver identity ---

ProtocolId WMbusTmodeDriver::protocol_id() const {
    return ProtocolId::WMbusT;
}

RadioProfileId WMbusTmodeDriver::required_radio_profile() const {
    return RadioProfileId::WMbusT868;
}

size_t WMbusTmodeDriver::max_session_encoded_bytes() const {
    return wmbus_tmode_rx::WmbusTmodeFramer::kMaxEncodedBytes;
}

// --- Session lifecycle ---

void WMbusTmodeDriver::reset_session() {
    framer_.reset();
    last_complete_result_ = wmbus_tmode_rx::FeedResult{};
    last_validated_       = ValidatedTelegram{};
    session_complete_     = false;
    last_decoded_         = false;
}

// --- Incremental byte feed ---

DriverFeedResult WMbusTmodeDriver::feed_byte(uint8_t byte) {
    const auto result = framer_.feed_byte(byte);
    last_complete_result_ = result;

    DriverFeedResult out{};

    switch (result.state) {
    case wmbus_tmode_rx::FramerState::NeedMoreData:
        out.status = DriverFeedStatus::NeedMoreData;
        break;

    case wmbus_tmode_rx::FramerState::CandidateViable: {
        out.status = DriverFeedStatus::CandidateViable;
        // Report remaining encoded bytes if the active candidate has resolved
        // the L-field. Prefer the normal orientation if both are active.
        const auto& pick = result.normal.active && result.normal.l_field_known
                               ? result.normal
                               : result.reversed;
        if (pick.l_field_known && pick.exact_encoded_bytes_required > pick.encoded_bytes_seen) {
            out.remaining_encoded_bytes = static_cast<uint16_t>(
                pick.exact_encoded_bytes_required - pick.encoded_bytes_seen);
        }
        break;
    }

    case wmbus_tmode_rx::FramerState::ExactFrameComplete:
        out.status                  = DriverFeedStatus::FrameComplete;
        out.remaining_encoded_bytes = 0;
        session_complete_           = true;
        break;

    case wmbus_tmode_rx::FramerState::CandidateRejected:
        out.status              = DriverFeedStatus::FrameRejected;
        out.reject_reason_code  = static_cast<uint8_t>(result.reject_reason);
        session_complete_       = false;
        last_decoded_           = false;
        break;
    }

    return out;
}

// --- Frame extraction ---

bool WMbusTmodeDriver::finalize_frame(ProtocolFrame& out_frame) {
    if (!session_complete_ || !last_complete_result_.has_complete_frame) {
        return false;
    }

    const auto& cand = last_complete_result_.frame;

    out_frame = ProtocolFrame{};
    out_frame.metadata.protocol      = ProtocolId::WMbusT;
    out_frame.metadata.radio_profile = RadioProfileId::WMbusT868;
    out_frame.metadata.end_reason    = FrameCaptureEndReason::Complete;

    const size_t enc_copy = std::min(static_cast<size_t>(cand.encoded_length),
                                     kProtocolFrameMaxEncodedBytes);
    const size_t dec_copy = std::min(static_cast<size_t>(cand.decoded_length),
                                     kProtocolFrameMaxDecodedBytes);

    std::memcpy(out_frame.encoded_bytes, cand.encoded_bytes.data(), enc_copy);
    out_frame.encoded_length = static_cast<uint16_t>(enc_copy);

    std::memcpy(out_frame.decoded_bytes, cand.decoded_bytes.data(), dec_copy);
    out_frame.decoded_length = static_cast<uint16_t>(dec_copy);

    out_frame.metadata.expected_encoded_length = cand.exact_encoded_bytes_required;
    out_frame.frame_complete                   = true;

    return true;
}

// --- Link-layer decode ---

bool WMbusTmodeDriver::decode_telegram(const ProtocolFrame& frame,
                                       DecodedTelegram& out_telegram) {
    last_decoded_ = false;

    if (!session_complete_ || !last_complete_result_.has_complete_frame) {
        return false;
    }

    const EncodedRxFrame rx_frame = build_encoded_rx_frame(frame);
    const auto link_result        = WmbusLink::validate_and_build(rx_frame);
    if (!link_result.accepted) {
        return false;
    }

    last_validated_ = link_result.telegram;
    last_decoded_   = true;

    fill_decoded_telegram(link_result.telegram, frame.metadata, out_telegram);
    return true;
}

const ValidatedTelegram* WMbusTmodeDriver::last_validated_telegram() const {
    return last_decoded_ ? &last_validated_ : nullptr;
}

// --- Private helpers ---

EncodedRxFrame WMbusTmodeDriver::build_encoded_rx_frame(
    const ProtocolFrame& frame) const {

    EncodedRxFrame rx{};
    const auto& cand = last_complete_result_.frame;

    rx.encoded_length               = cand.encoded_length;
    rx.decoded_length               = cand.decoded_length;
    rx.exact_encoded_bytes_required = cand.exact_encoded_bytes_required;
    rx.l_field                      = cand.l_field;
    rx.orientation                  = cand.orientation;
    rx.first_block_validation       = cand.first_block_validation;

    std::memcpy(rx.encoded_bytes.data(), cand.encoded_bytes.data(), cand.encoded_length);
    std::memcpy(rx.decoded_bytes.data(), cand.decoded_bytes.data(), cand.decoded_length);

    rx.metadata.rssi_dbm                  = frame.metadata.rssi_dbm;
    rx.metadata.lqi                       = frame.metadata.lqi;
    rx.metadata.crc_ok                    = frame.metadata.radio_crc_ok;
    rx.metadata.radio_crc_available       = frame.metadata.radio_crc_available;
    rx.metadata.exact_frame_contract_valid = frame.frame_complete;
    rx.metadata.transitional_raw_adapter_used = false;
    rx.metadata.timestamp_ms              = frame.metadata.timestamp_ms;
    rx.metadata.capture_elapsed_ms        = frame.metadata.capture_elapsed_ms;
    rx.metadata.captured_frame_length     = cand.encoded_length;

    return rx;
}

// static
void WMbusTmodeDriver::fill_decoded_telegram(
    const ValidatedTelegram& vt,
    const ProtocolFrameMetadata& meta,
    DecodedTelegram& out) {

    out = DecodedTelegram{};

    out.identity.device_id       = vt.device_id();
    out.identity.manufacturer_id = vt.manufacturer_id();
    out.identity.reliable        = vt.has_reliable_identity();
    // Canonical bytes follow the W-MBus decoded layout:
    //   [0]=L [1]=C [2..3]=Mfg [4..7]=DevID [8]=Version [9]=DevType
    if (vt.link.metadata.canonical_length >= 10U) {
        out.identity.device_version = vt.link.canonical_bytes[8];
        out.identity.device_type    = vt.link.canonical_bytes[9];
    }
    out.has_reliable_identity = vt.has_reliable_identity();

    out.metadata.protocol       = ProtocolId::WMbusT;
    out.metadata.radio_instance = meta.radio_instance;
    out.metadata.radio_profile  = meta.radio_profile;
    out.metadata.rssi_dbm       = meta.rssi_dbm;
    out.metadata.lqi            = meta.lqi;
    out.metadata.timestamp_ms   = meta.timestamp_ms;
    out.metadata.encoded_length         = vt.link.metadata.encoded_length;
    out.metadata.exact_encoded_expected = vt.link.metadata.exact_encoded_bytes_required;
    out.metadata.canonical_length       = vt.link.metadata.canonical_length;

    out.canonical_length = vt.link.metadata.canonical_length;
    const size_t clen = std::min(static_cast<size_t>(out.canonical_length),
                                 kDecodedTelegramMaxCanonicalBytes);
    std::memcpy(out.canonical_bytes, vt.link.canonical_bytes.data(), clen);
}

} // namespace wmbus_link
