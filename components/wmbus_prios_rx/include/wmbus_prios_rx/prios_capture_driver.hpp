#pragma once

#include "protocol_driver/i_protocol_driver.hpp"

#include <cstdint>

// PriosCaptureDriver: IProtocolDriver stub for PRIOS R3 bring-up.
//
// This is NOT a decoder. It implements the IProtocolDriver interface purely
// in "capture" mode: it accepts raw bytes up to a bounded budget per session
// and packages them as a ProtocolFrame for diagnostic storage.
//
// decode_telegram() always returns false — no link-layer decoding exists yet.
// Once sufficient hardware captures have been gathered and the PRIOS frame
// structure is confirmed, this driver will be extended to a real decoder.
//
// Usage sequence:
//   1. reset_session()
//   2. feed_byte() * N  →  NeedMoreData until kMaxCaptureBytes reached,
//                           then FrameComplete
//   3. finalize_frame() →  ProtocolFrame with raw captured bytes in
//                           encoded_bytes; decoded_length=0
//   4. decode_telegram() → always false (no decoder)
//
// The IProtocolDriver interface is satisfied so this driver can be wired to
// the session engine in a future migration step without API changes.

namespace wmbus_prios_rx {

class PriosCaptureDriver final : public protocol_driver::IProtocolDriver {
  public:
    // Bounded byte budget per capture session.
    // Large enough to hold one complete PRIOS frame candidate (size unknown
    // at this stage) while small enough to avoid excessive RAM use.
    static constexpr uint16_t kMaxCaptureBytes = 64;

    PriosCaptureDriver() = default;

    // --- IProtocolDriver identity ---

    protocol_driver::ProtocolId     protocol_id() const override;
    protocol_driver::RadioProfileId required_radio_profile() const override;
    size_t                          max_session_encoded_bytes() const override;

    // --- Session lifecycle ---

    void reset_session() override;

    // --- Incremental byte feed ---

    // Accumulates raw bytes without any decoding. Returns FrameComplete once
    // kMaxCaptureBytes have been received; NeedMoreData before that.
    protocol_driver::DriverFeedResult feed_byte(uint8_t byte) override;

    // --- Frame extraction ---

    // Fills out_frame.encoded_bytes with the raw captured bytes.
    // out_frame.decoded_length is always 0 (no PHY decoding in bring-up mode).
    // Returns false if the session has not reached FrameComplete yet.
    bool finalize_frame(protocol_driver::ProtocolFrame& out_frame) override;

    // --- Link-layer decode (not implemented) ---

    // Always returns false. PRIOS decoding is not implemented until verified
    // frame structure and CRC scheme are confirmed from hardware captures.
    bool decode_telegram(const protocol_driver::ProtocolFrame& frame,
                         protocol_driver::DecodedTelegram& out_telegram) override;

    // Returns total bytes captured in the last completed session.
    // Zero before the first FrameComplete or after reset_session().
    uint16_t captured_length() const;

  private:
    uint8_t  buf_[kMaxCaptureBytes]{};
    uint16_t len_      = 0;
    bool     complete_ = false;
};

} // namespace wmbus_prios_rx
