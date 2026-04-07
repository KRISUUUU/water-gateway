#pragma once

#include "protocol_driver/protocol_ids.hpp"
#include "wmbus_prios_rx/prios_capture_service.hpp"

#include <cstddef>
#include <cstdint>

// PriosDecoder: identity extractor for a PRIOS R3 raw capture.
//
// Produces a PriosDecodedTelegram from a PriosCaptureRecord using ONLY the
// field positions empirically confirmed during the bring-up campaign:
//   - Bytes 9–14: stable per-device identity (device fingerprint / serial).
//
// No reading value is extracted: PRIOS R3 payload encoding has not been
// reverse-engineered to a sufficient level of confidence in this codebase.
// decoded_ok is therefore always false.  All downstream consumers (registry,
// MQTT, UI) must handle a telegram with identity but without a reading.
//
// This is a pure, allocation-free, host-testable function.

namespace wmbus_prios_rx {

struct PriosDecodedTelegram {
    // Max raw bytes shown as hex in display_prefix_hex (matches kDisplayPrefixBytes).
    static constexpr size_t kDisplayPrefixRawBytes = PriosCaptureRecord::kDisplayPrefixBytes;

    bool valid = false;  // false when capture is too short or not PRIOS
    protocol_driver::RadioProfileId radio_profile =
        protocol_driver::RadioProfileId::WMbusPriosR3;

    // Identity: meter ID as 8-char hex string, e.g. "12345678"
    char meter_key[16]{};

    uint32_t meter_id = 0;
    uint16_t manufacturer_id = 0;
    char manufacturer[4]{}; // e.g. "SAP"

    bool encrypted = false;

    // Display prefix: up to kDisplayPrefixRawBytes raw captured bytes as hex.
    // Used as the "raw_hex" representation in the recent-telegrams list.
    char display_prefix_hex[kDisplayPrefixRawBytes * 2u + 1u]{};
    uint8_t display_prefix_length = 0;  // number of hex pairs in display_prefix_hex

    // Radio metadata (copied from the capture record).
    int8_t   rssi_dbm          = 0;
    uint8_t  lqi               = 0;
    int64_t  timestamp_ms      = 0;
    uint32_t sequence          = 0;
    bool     manchester_enabled = false;
    uint16_t captured_length   = 0;
};

class PriosDecoder {
  public:
    // Decode one raw capture record into a normalized PriosDecodedTelegram.
    // Returns a telegram with valid=false when the capture is too short to
    // extract the device fingerprint (< 15 bytes).
    [[nodiscard]] static PriosDecodedTelegram decode(const PriosCaptureRecord& record);
};

} // namespace wmbus_prios_rx
