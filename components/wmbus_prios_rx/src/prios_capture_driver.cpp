#include "wmbus_prios_rx/prios_capture_driver.hpp"

#include <algorithm>
#include <cstring>

namespace wmbus_prios_rx {

using namespace protocol_driver;

ProtocolId PriosCaptureDriver::protocol_id() const {
    return ProtocolId::WMbusPrios;
}

RadioProfileId PriosCaptureDriver::required_radio_profile() const {
    return RadioProfileId::WMbusPriosR3;
}

size_t PriosCaptureDriver::max_session_encoded_bytes() const {
    return kMaxCaptureBytes;
}

void PriosCaptureDriver::reset_session() {
    len_      = 0;
    complete_ = false;
}

DriverFeedResult PriosCaptureDriver::feed_byte(uint8_t byte) {
    DriverFeedResult out{};

    if (complete_) {
        // Caller must call reset_session() after FrameComplete before feeding
        // more bytes. Treat as already-complete.
        out.status = DriverFeedStatus::FrameComplete;
        return out;
    }

    if (len_ < kMaxCaptureBytes) {
        buf_[len_++] = byte;
    }

    if (len_ >= kMaxCaptureBytes) {
        complete_  = true;
        out.status = DriverFeedStatus::FrameComplete;
    } else {
        out.status = DriverFeedStatus::NeedMoreData;
    }

    return out;
}

bool PriosCaptureDriver::finalize_frame(ProtocolFrame& out_frame) {
    if (!complete_) {
        return false;
    }

    out_frame = ProtocolFrame{};
    out_frame.metadata.protocol      = ProtocolId::WMbusPrios;
    out_frame.metadata.radio_profile = RadioProfileId::WMbusPriosR3;
    out_frame.metadata.end_reason    = FrameCaptureEndReason::Complete;

    const size_t copy = std::min(static_cast<size_t>(len_), kProtocolFrameMaxEncodedBytes);
    std::memcpy(out_frame.encoded_bytes, buf_, copy);
    out_frame.encoded_length = static_cast<uint16_t>(copy);

    // No PHY-level decode in bring-up mode.
    out_frame.decoded_length = 0;
    out_frame.frame_complete = true;

    return true;
}

bool PriosCaptureDriver::decode_telegram(const ProtocolFrame& /*frame*/,
                                          DecodedTelegram& /*out_telegram*/) {
    // PRIOS link-layer decoding is not implemented.
    // Returns false until frame structure and CRC scheme are confirmed from
    // hardware captures and an actual decoder is written.
    return false;
}

uint16_t PriosCaptureDriver::captured_length() const {
    return len_;
}

} // namespace wmbus_prios_rx
