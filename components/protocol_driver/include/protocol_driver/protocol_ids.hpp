#pragma once

#include <cstdint>

// Protocol and radio identity types shared across the multi-protocol architecture.
//
// Design intent:
//   - ProtocolId names the protocol/framing standard (T-mode, PRIOS, ...).
//   - RadioInstanceId names the physical radio instance. Right now only the
//     primary (index 0) is wired; index 1 is reserved for a second CC1101.
//   - RadioProfileId names a CC1101 configuration (frequency, baud-rate,
//     modulation) that a protocol requires. One profile may serve multiple
//     protocols if their RF parameters happen to match.
//
// These enums are stable identifiers used in scheduler entries, session
// metadata, diagnostics records, and decoded telegram metadata. They must
// not carry protocol-parsing logic.

namespace protocol_driver {

// ----- ProtocolId ----

enum class ProtocolId : uint8_t {
    Unknown    = 0,
    WMbusT     = 1, // Wireless M-Bus T1/T2 mode (OOK, 868.95 MHz, 32.768 kbaud)
    WMbusPrios = 2, // PRIOS / Sensus variant (OOK, 868 MHz band, exact params TBD)
};

// Human-readable label for logging and diagnostics; never for protocol logic.
inline const char* protocol_id_to_string(ProtocolId id) {
    switch (id) {
        case ProtocolId::WMbusT:     return "WMbusT";
        case ProtocolId::WMbusPrios: return "WMbusPrios";
        default:                     return "Unknown";
    }
}

// ----- RadioInstanceId -----

// Physical radio slot index. 0 = primary CC1101 (present), 1 = secondary
// (future second CC1101). Using a plain uint8_t alias keeps the type
// lightweight and array-indexable.
using RadioInstanceId = uint8_t;

static constexpr RadioInstanceId kRadioInstancePrimary   = 0;
static constexpr RadioInstanceId kRadioInstanceSecondary = 1;

// ----- RadioProfileId -----

// Identifies a CC1101 register configuration. Each profile corresponds to
// one distinct set of RF parameters. The scheduler uses this to know which
// configuration to apply to the radio when switching protocols.
enum class RadioProfileId : uint8_t {
    Unknown        = 0,
    WMbusT868      = 1, // 868.95 MHz OOK, 32.768 kbaud (T-mode, current)
    WMbusPriosR3   = 2, // PRIOS R3 variant, 868 MHz OOK, exact params TBD
    WMbusPriosR4   = 3, // PRIOS R4 variant, 868 MHz OOK, exact params TBD
};

inline const char* radio_profile_id_to_string(RadioProfileId id) {
    switch (id) {
        case RadioProfileId::WMbusT868:    return "WMbusT868";
        case RadioProfileId::WMbusPriosR3: return "WMbusPriosR3";
        case RadioProfileId::WMbusPriosR4: return "WMbusPriosR4";
        default:                           return "Unknown";
    }
}

// ----- RadioSchedulerMode -----

// Controls how the radio owner task cycles through enabled profiles.
//   Locked   — one selected profile only; the radio never leaves it.
//   Priority — one preferred profile with bounded fallback scanning.
//   Scan     — round-robin across all enabled profiles.
//
// All three modes are wired into the single-radio owner task. Priority and
// Scan advance only on bounded idle/liveness wakes so one radio instance
// still owns exactly one active RX path at a time.
enum class RadioSchedulerMode : uint8_t {
    Locked   = 0,
    Priority = 1,
    Scan     = 2,
};

inline const char* radio_scheduler_mode_to_string(RadioSchedulerMode mode) {
    switch (mode) {
        case RadioSchedulerMode::Locked:   return "Locked";
        case RadioSchedulerMode::Priority: return "Priority";
        case RadioSchedulerMode::Scan:     return "Scan";
        default:                           return "Unknown";
    }
}

// ----- RadioProfileMask -----

// Bitmask of enabled RadioProfileId values: bit N = (1 << N) where N is the
// RadioProfileId enum value. Bit 0 (Unknown) is never set.
using RadioProfileMask = uint8_t;

static constexpr RadioProfileMask kRadioProfileMaskNone         = 0x00;
static constexpr RadioProfileMask kRadioProfileMaskWMbusT868    = 0x02; // 1 << WMbusT868(1)
static constexpr RadioProfileMask kRadioProfileMaskWMbusPriosR3 = 0x04; // 1 << WMbusPriosR3(2)
static constexpr RadioProfileMask kRadioProfileMaskWMbusPriosR4 = 0x08; // 1 << WMbusPriosR4(3)

} // namespace protocol_driver
