#pragma once

#include "protocol_driver/protocol_ids.hpp"

#include <cstddef>
#include <cstdint>

// ProtocolFrame: the output of a completed RX capture session before
// link-layer validation.
//
// This type represents "exactly one candidate encoded PHY frame" as required
// by Contract A in docs/rf_refactor_target.md. It is protocol-agnostic at
// this level; the protocol field names the framing standard that produced it.
//
// Size budget: the largest valid W-MBus frame is well under 256 encoded bytes.
// 290 bytes covers T-mode with maximum block structure plus modest headroom.

namespace protocol_driver {

static constexpr size_t kProtocolFrameMaxEncodedBytes = 290;
static constexpr size_t kProtocolFrameMaxDecodedBytes = 256;

// Reason a frame capture ended before a complete frame was assembled.
// Protocol-driver-specific codes go in the driver; this enum covers the
// common session-level reasons.
enum class FrameCaptureEndReason : uint8_t {
    Complete           = 0, // Full frame captured; frame_valid indicates link acceptance
    CandidateRejected  = 1, // Framer rejected the candidate (invalid symbol, bad length, …)
    NoProgressTimeout  = 2, // No new bytes arrived within the watchdog window
    RadioOverflow      = 3, // FIFO overflow before frame was complete
    RadioError         = 4, // SPI or radio hardware failure
};

struct ProtocolFrameMetadata {
    RadioInstanceId radio_instance = kRadioInstancePrimary;
    RadioProfileId  radio_profile  = RadioProfileId::Unknown;
    ProtocolId      protocol       = ProtocolId::Unknown;

    int8_t  rssi_dbm           = 0;
    uint8_t lqi                = 0;
    bool    radio_crc_ok       = false;
    bool    radio_crc_available = false;

    // Monotonic millisecond timestamp of capture start (from esp_timer or stub).
    int64_t  timestamp_ms      = 0;
    uint16_t capture_elapsed_ms = 0;

    FrameCaptureEndReason end_reason = FrameCaptureEndReason::Complete;

    // Count of encoded bytes the framer expected based on the L-field.
    // Zero if the L-field was never resolved.
    uint16_t expected_encoded_length = 0;
};

struct ProtocolFrame {
    ProtocolFrameMetadata metadata{};

    uint8_t  encoded_bytes[kProtocolFrameMaxEncodedBytes]{};
    uint16_t encoded_length = 0;

    // Decoded bytes if the framing layer produced them (e.g. 3-of-6 output).
    // May be empty for protocols that do not have a PHY-level encoding step.
    uint8_t  decoded_bytes[kProtocolFrameMaxDecodedBytes]{};
    uint16_t decoded_length = 0;

    // True when end_reason == Complete and the framer reached the exact
    // encoded byte budget. Does not imply link-layer validity.
    bool frame_complete = false;
};

} // namespace protocol_driver
