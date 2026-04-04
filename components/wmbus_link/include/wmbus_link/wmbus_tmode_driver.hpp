#pragma once

#include "protocol_driver/i_protocol_driver.hpp"
#include "wmbus_link/wmbus_link.hpp"
#include "wmbus_tmode_rx/wmbus_tmode_framer.hpp"

#include <cstddef>
#include <cstdint>

// WMbusTmodeDriver: concrete IProtocolDriver for Wireless M-Bus T1/T2 mode.
//
// This is Protocol Driver #1 in the multi-protocol architecture. It wraps:
//   - WmbusTmodeFramer  — incremental 3-of-6 framing (from wmbus_tmode_rx)
//   - WmbusLink::validate_and_build() — link-layer CRC/block validation
//   - Conversion to protocol_driver::DecodedTelegram for the router layer
//
// Component placement rationale:
//   This class lives in wmbus_link (not wmbus_tmode_rx) to avoid a circular
//   dependency: wmbus_link already depends on wmbus_tmode_rx; the reverse
//   direction would create a cycle.
//
// Usage sequence within one session:
//   1. reset_session()
//   2. feed_byte() * N  until FrameComplete or FrameRejected
//   3. finalize_frame() — extracts ProtocolFrame; stores T-mode framing state
//   4. decode_telegram() — validates link layer; produces DecodedTelegram
//      (Signal quality must be set in frame.metadata before this call.)
//   5. [optionally] last_validated_telegram() — for callers still using
//      wmbus_link::ValidatedTelegram (e.g. existing router/MQTT path)
//   6. reset_session() before next session
//
// This driver does not perform SPI, GPIO, or FreeRTOS operations.

namespace wmbus_link {

class WMbusTmodeDriver final : public protocol_driver::IProtocolDriver {
  public:
    WMbusTmodeDriver() = default;

    // --- IProtocolDriver identity ---

    protocol_driver::ProtocolId      protocol_id() const override;
    protocol_driver::RadioProfileId  required_radio_profile() const override;
    size_t                           max_session_encoded_bytes() const override;

    // --- Session lifecycle ---

    void reset_session() override;

    // --- Incremental byte feed ---

    protocol_driver::DriverFeedResult feed_byte(uint8_t byte) override;

    // --- Frame extraction ---

    // Fills out_frame from the last FrameComplete feed result. Also preserves
    // T-mode-specific framing state (orientation, L-field, block validation)
    // needed by decode_telegram().
    bool finalize_frame(protocol_driver::ProtocolFrame& out_frame) override;

    // --- Link-layer decode ---

    // Calls WmbusLink::validate_and_build() on the internally stored framing
    // state. Metadata (signal quality, timestamp) is taken from frame.metadata.
    // Returns true on success; out_telegram is filled.
    bool decode_telegram(const protocol_driver::ProtocolFrame& frame,
                         protocol_driver::DecodedTelegram& out_telegram) override;

    // Accessor for callers that still need wmbus_link::ValidatedTelegram
    // (e.g. the existing router and MQTT path during incremental migration).
    // Returns nullptr if decode_telegram() was not called or returned false.
    const ValidatedTelegram* last_validated_telegram() const;

  private:
    wmbus_tmode_rx::WmbusTmodeFramer framer_{};
    wmbus_tmode_rx::FeedResult       last_complete_result_{};
    ValidatedTelegram                last_validated_{};
    bool                             session_complete_ = false;
    bool                             last_decoded_     = false;

    // Reconstruct an EncodedRxFrame from the stored framing state + frame
    // signal-quality metadata. Used internally by decode_telegram().
    EncodedRxFrame build_encoded_rx_frame(
        const protocol_driver::ProtocolFrame& frame) const;

    // Fill a DecodedTelegram from a validated telegram and frame metadata.
    static void fill_decoded_telegram(
        const ValidatedTelegram& vt,
        const protocol_driver::ProtocolFrameMetadata& meta,
        protocol_driver::DecodedTelegram& out);
};

} // namespace wmbus_link
