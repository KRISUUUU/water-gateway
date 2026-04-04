#pragma once

#include "protocol_driver/protocol_ids.hpp"

#include <cstddef>
#include <cstdint>

// DecodedTelegram: the normalized, protocol-agnostic output of a successful
// link-layer validation step.
//
// This type corresponds to Contract C in docs/rf_refactor_target.md.
// Protocol-specific rich details (block structure, CRC breakdown, etc.) live
// in protocol-specific types; this struct carries only the fields that the
// router, dedup, registry, and MQTT layers need.
//
// The canonical_bytes field holds the fully decoded application payload bytes
// (after any PHY encoding reversal and after CRC bytes are stripped), ready
// for dedup keying and downstream consumption.

namespace protocol_driver {

static constexpr size_t kDecodedTelegramMaxCanonicalBytes = 256;

struct DecodedTelegramIdentity {
    uint32_t device_id       = 0; // Meter serial number (BCD-encoded in wire format)
    uint16_t manufacturer_id = 0; // 3-letter manufacturer code, 5-bit packed
    uint8_t  device_type     = 0; // EN 13757-3 device type byte
    uint8_t  device_version  = 0; // Protocol version byte
    bool     reliable        = false; // True if identity was verified by CRC
};

struct DecodedTelegramMetadata {
    RadioInstanceId radio_instance = kRadioInstancePrimary;
    RadioProfileId  radio_profile  = RadioProfileId::Unknown;
    ProtocolId      protocol       = ProtocolId::Unknown;

    int8_t  rssi_dbm    = 0;
    uint8_t lqi         = 0;
    int64_t timestamp_ms = 0;

    // Size of the encoded frame that produced this telegram.
    uint16_t encoded_length          = 0;
    uint16_t exact_encoded_expected  = 0;
    uint16_t canonical_length        = 0;
};

struct DecodedTelegram {
    DecodedTelegramIdentity identity{};
    DecodedTelegramMetadata metadata{};

    uint8_t  canonical_bytes[kDecodedTelegramMaxCanonicalBytes]{};
    uint16_t canonical_length = 0;

    // True when identity.reliable == true and identity.device_id != 0.
    bool has_reliable_identity = false;
};

} // namespace protocol_driver
