#pragma once

#include "protocol_driver/decoded_telegram.hpp"
#include "protocol_driver/protocol_frame.hpp"
#include "protocol_driver/protocol_ids.hpp"

#include <cstddef>
#include <cstdint>

// IProtocolDriver: abstract interface for a single-protocol receive driver.
//
// Responsibilities of a concrete driver:
//   - Declare which ProtocolId and RadioProfileId it requires.
//   - Accept raw encoded bytes incrementally via feed_byte().
//   - Report framing progress through typed DriverFeedStatus values
//     (Contract B in docs/rf_refactor_target.md).
//   - When the frame is complete, produce a ProtocolFrame via finalize_frame()
//     and optionally a DecodedTelegram via decode_telegram() after link
//     validation has been applied.
//   - Reset internal state cleanly between sessions via reset_session().
//
// Non-responsibilities:
//   - No SPI/GPIO access. The session engine handles radio I/O.
//   - No MQTT publishing or routing decisions.
//   - No long-term diagnostics storage; the session engine records diagnostics.
//
// Concrete implementations:
//   - WMbusTmodeDriver (wmbus_tmode_rx) — wraps WmbusTmodeFramer + RxSessionEngine
//     (migration target, not yet complete)
//   - WMbusPriosDriver (wmbus_prios_rx) — scaffolding, no decoding yet

namespace protocol_driver {

// Status returned from feed_byte(). Mirrors the framer's FramerState concept
// but is expressed at the protocol-driver level so the session engine does not
// need to know which concrete framer is in use.
enum class DriverFeedStatus : uint8_t {
    NeedMoreData      = 0, // Frame not yet complete; keep feeding
    CandidateViable   = 1, // L-field resolved; exact byte budget is known
    FrameComplete     = 2, // Exact encoded frame assembled; call finalize_frame()
    FrameRejected     = 3, // Candidate is invalid; call reset_session()
};

struct DriverFeedResult {
    DriverFeedStatus status = DriverFeedStatus::NeedMoreData;

    // Protocol-specific rejection code, for diagnostics only. Zero when not
    // rejected. Interpretation is driver-defined.
    uint8_t reject_reason_code = 0;

    // When CandidateViable or FrameComplete, the exact number of encoded bytes
    // the driver still expects (0 if already complete).
    uint16_t remaining_encoded_bytes = 0;
};

class IProtocolDriver {
  public:
    virtual ~IProtocolDriver() = default;

    // --- Identity ---

    // The protocol this driver handles. Stable for the lifetime of the driver.
    virtual ProtocolId protocol_id() const = 0;

    // The radio profile this driver requires. Used by the scheduler to
    // configure the CC1101 before handing a session to this driver.
    virtual RadioProfileId required_radio_profile() const = 0;

    // Upper bound on encoded bytes this driver will accept in one session.
    // The session engine uses this to size buffers and abort oversized captures.
    virtual size_t max_session_encoded_bytes() const = 0;

    // --- Session lifecycle ---

    // Clear all incremental framing state. Must be called before each new
    // receive session and after FrameRejected or FrameComplete.
    virtual void reset_session() = 0;

    // --- Incremental byte feed ---

    // Feed one encoded byte into the driver. Returns status and optional
    // metadata about the framing progress. The caller must not feed bytes
    // after FrameComplete or FrameRejected without calling reset_session().
    virtual DriverFeedResult feed_byte(uint8_t byte) = 0;

    // --- Frame extraction ---

    // Called after feed_byte() returns FrameComplete. Fills out_frame with
    // the complete encoded/decoded frame and metadata. The driver may store
    // internal state (orientation, framing artifacts) that decode_telegram()
    // will use — so finalize_frame() and decode_telegram() must be called in
    // order within the same session, without an intervening reset_session().
    // Returns true if a complete frame is available; false otherwise.
    virtual bool finalize_frame(ProtocolFrame& out_frame) = 0;

    // --- Link-layer decode (optional per driver) ---

    // Called after finalize_frame(). Performs link-layer validation on the
    // captured frame and, if valid, fills out_telegram with the decoded
    // telegram. The driver may use post-finalize internal state (e.g. framing
    // orientation) that is not present in the ProtocolFrame alone.
    // Returns true on success. Drivers without full link-layer support return
    // false.
    virtual bool decode_telegram(const ProtocolFrame& frame,
                                 DecodedTelegram& out_telegram) = 0;
};

} // namespace protocol_driver
