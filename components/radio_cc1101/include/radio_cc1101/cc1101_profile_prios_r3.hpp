#pragma once

#include "radio_cc1101/cc1101_profile_tmode.hpp"
#include "radio_cc1101/cc1101_registers.hpp"

#include <cstddef>
#include <cstdint>

// PRIOS R3 CC1101 bring-up profiles — two capture variants.
//
// *** EXPERIMENTAL — ALL VALUES ARE UNCONFIRMED ***
//
// This profile targets the documented PRIOS R3 operating point:
//   868.95 MHz, FSK-family, ~32.768 kbaud
//
// The register values below are an INITIAL HYPOTHESIS derived from the
// T-mode profile at the same frequency and baud rate. They have NOT been
// validated against real PRIOS hardware captures.
//
// Known unknowns that must be resolved from hardware captures:
//   - SYNC1/SYNC0: placeholder from T-mode (0x54/0x3D). Likely WRONG.
//   - DEVIATN (FSK deviation): T-mode value kept; may differ for PRIOS.
//   - MANCHESTER_EN (MDMCFG2 bit 3): status unknown; both variants provided.
//   - MDMCFG4/3 (baud rate): T-mode 32.768 kbaud kept; may need adjustment.
//
// SYNC_MODE is set to 001 (15/16 bits) rather than T-mode's 011 (30/32 bits)
// to be slightly more permissive during bring-up, tolerating 1 bit error in
// the sync word. This does NOT help if the PRIOS sync word is completely
// different from the placeholder.
//
// Two variants are provided so both can be tried in the field:
//   Variant A (kPriosR3Config):           Manchester disabled — MDMCFG2 = 0x01
//   Variant B (kPriosR3ConfigManchesterOn): Manchester enabled — MDMCFG2 = 0x0A
//
// Use prios_r3_profile(manchester_enabled, out_count) for the current
// sync-driven campaign path, or prios_r3_discovery_profile(...) for the
// discovery/sniffer path that avoids relying on the placeholder sync word.
//
// Update procedure once evidence is available:
//   1. Capture raw PRIOS frames using the bring-up driver for both variants.
//   2. Run PriosAnalyzer; compare prefix stability between variants.
//   3. Update SYNC1/SYNC0 and MDMCFG2 (Manchester, sync mode) accordingly.
//   4. Remove EXPERIMENTAL comment once values are confirmed.

namespace radio_cc1101 {

// PRIOS R3 uses the same register entry layout as T-mode.
using PriosR3RegisterConfig = TmodeRegisterConfig;

// ---- Variant A: Manchester disabled (MDMCFG2 bit 3 = 0) --------------------

static constexpr PriosR3RegisterConfig kPriosR3Config[] = {
    {registers::IOCFG2,   0x06},   // GDO2: sync word sent/received
    {registers::IOCFG0,   0x00},   // GDO0: RX FIFO threshold (same as T-mode)
    {registers::FIFOTHR,  0x47},   // RX FIFO threshold: 33 bytes
    // SYNC word: EXPERIMENTAL placeholder from T-mode. Almost certainly wrong
    // for PRIOS. Will be corrected after first hardware captures.
    {registers::SYNC1,    0x54},   // EXPERIMENTAL — T-mode placeholder
    {registers::SYNC0,    0x3D},   // EXPERIMENTAL — T-mode placeholder
    {registers::PKTLEN,   0xFF},   // Upper bound for FIFO protection
    {registers::PKTCTRL1, 0x00},   // No address filtering
    {registers::PKTCTRL0, 0x02},   // Infinite packet length, CRC disabled
    {registers::FSCTRL1,  0x08},   // IF frequency (T-mode value, EXPERIMENTAL)
    {registers::FSCTRL0,  0x00},   // Frequency offset
    // 868.95 MHz — same as T-mode, confirmed for PRIOS R3
    {registers::FREQ2,    0x21},
    {registers::FREQ1,    0x6B},
    {registers::FREQ0,    0xD1},
    // 32.768 kbaud — EXPERIMENTAL (T-mode value; PRIOS baud may differ)
    {registers::MDMCFG4,  0x5B},   // Channel BW ~325 kHz, DRATE_E=11
    {registers::MDMCFG3,  0xF8},   // DRATE_M=248 → ~32.768 kbaud; EXPERIMENTAL
    // MDMCFG2 = 0x01:
    //   MOD_FORMAT[6:4]=000 = 2-FSK
    //   MANCHESTER_EN[3]=0  = Manchester OFF  (Variant A)
    //   SYNC_MODE[2:0]=001  = 15/16 bits (more permissive than T-mode's 30/32)
    {registers::MDMCFG2,  0x01},   // EXPERIMENTAL — Variant A (Manchester off)
    {registers::MDMCFG1,  0x22},   // 4 preamble bytes, FEC disabled
    {registers::MDMCFG0,  0xF8},   // Channel spacing
    // FSK deviation ~47.607 kHz — EXPERIMENTAL (T-mode value)
    {registers::DEVIATN,  0x47},   // EXPERIMENTAL
    {registers::MCSM1,    0x3F},   // Stay in RX after RX/TX
    {registers::MCSM0,    0x18},   // Autocal on idle→RX/TX
    {registers::FOCCFG,   0x1D},
    {registers::BSCFG,    0x1C},
    {registers::AGCCTRL2, 0xC7},
    {registers::AGCCTRL1, 0x00},
    {registers::AGCCTRL0, 0xB2},
    {registers::FREND1,   0xB6},
    {registers::FREND0,   0x10},
    {registers::FSCAL3,   0xEA},
    {registers::FSCAL2,   0x2A},
    {registers::FSCAL1,   0x00},
    {registers::FSCAL0,   0x1F},
    {registers::TEST2,    0x81},
    {registers::TEST1,    0x35},
    {registers::TEST0,    0x09},
};

static constexpr size_t kPriosR3ConfigSize =
    sizeof(kPriosR3Config) / sizeof(kPriosR3Config[0]);

// ---- Variant B: Manchester enabled (MDMCFG2 bit 3 = 1) ---------------------
// Variant B remains stricter than Variant A, but is intentionally set to a
// middle-ground sync gate after 30/32 proved too selective near the meter.
// It keeps Manchester ON and now uses exact 16/16-bit sync matching so the
// operator can gather a larger candidate set without reopening the old flood.

static constexpr PriosR3RegisterConfig kPriosR3ConfigManchesterOn[] = {
    {registers::IOCFG2,   0x06},
    {registers::IOCFG0,   0x00},
    {registers::FIFOTHR,  0x47},
    {registers::SYNC1,    0x54},   // EXPERIMENTAL — T-mode placeholder
    {registers::SYNC0,    0x3D},   // EXPERIMENTAL — T-mode placeholder
    {registers::PKTLEN,   0xFF},
    {registers::PKTCTRL1, 0x00},
    {registers::PKTCTRL0, 0x02},
    {registers::FSCTRL1,  0x08},
    {registers::FSCTRL0,  0x00},
    {registers::FREQ2,    0x21},
    {registers::FREQ1,    0x6B},
    {registers::FREQ0,    0xD1},
    {registers::MDMCFG4,  0x5B},
    {registers::MDMCFG3,  0xF8},
    // MDMCFG2 = 0x0A:
    //   MOD_FORMAT[6:4]=000 = 2-FSK
    //   MANCHESTER_EN[3]=1  = Manchester ON   (Variant B)
    //   SYNC_MODE[2:0]=010  = exact 16/16 bits (between Variant A and prior 30/32 tuning)
    {registers::MDMCFG2,  0x0A},   // EXPERIMENTAL — Variant B (Manchester on, middle-ground sync)
    {registers::MDMCFG1,  0x22},
    {registers::MDMCFG0,  0xF8},
    {registers::DEVIATN,  0x47},
    {registers::MCSM1,    0x3F},
    {registers::MCSM0,    0x18},
    {registers::FOCCFG,   0x1D},
    {registers::BSCFG,    0x1C},
    {registers::AGCCTRL2, 0xC7},
    {registers::AGCCTRL1, 0x00},
    {registers::AGCCTRL0, 0xB2},
    {registers::FREND1,   0xB6},
    {registers::FREND0,   0x10},
    {registers::FSCAL3,   0xEA},
    {registers::FSCAL2,   0x2A},
    {registers::FSCAL1,   0x00},
    {registers::FSCAL0,   0x1F},
    {registers::TEST2,    0x81},
    {registers::TEST1,    0x35},
    {registers::TEST0,    0x09},
};

static constexpr size_t kPriosR3ConfigManchesterOnSize =
    sizeof(kPriosR3ConfigManchesterOn) / sizeof(kPriosR3ConfigManchesterOn[0]);

// ---- Discovery/sniffer variants --------------------------------------------
//
// "Preamble Sniffer" mode: the hardware sync engine is repurposed as a
// preamble detector by loading the FSK idle pattern (0xAA/0xAA) as the sync
// word.  This gives us hardware bit-alignment and a clean capture window
// instead of the carrier-sense approach (IOCFG2=0x0E / SYNC_MODE=100) that
// was producing 100 % quality rejections because:
//   a) bit alignment was arbitrary, and
//   b) RSSI was sampled at end-of-burst, already in the noise floor.
//
// How it works:
//   - SYNC1=0xAA, SYNC0=0xAA → the CC1101 locks on the standard 2-FSK
//     preamble that every wM-Bus transmitter emits before its real payload.
//   - MDMCFG2=0x02 → 2-FSK, Manchester OFF, SYNC_MODE=010 (exact 16/16 bits).
//     The 0xAAAA pattern is a "master key" — it matches any transmitter on
//     this channel regardless of its actual sync word.
//   - IOCFG2=0x06 → GDO2 pulses on sync-word match, giving us a well-aligned
//     capture start with a valid RSSI reading.
//
// The bytes captured immediately after the preamble are the real PRIOS sync
// word.  Collect several captures and look for the stable prefix — that is
// the value to promote to SYNC1/SYNC0 in the campaign profiles.
//
// *** TEMPORARY — 0xAAAA is a reverse-engineering master key, NOT a
//     confirmed PRIOS sync word.  Remove once the real word is found. ***

static constexpr PriosR3RegisterConfig kPriosR3DiscoveryConfig[] = {
    {registers::IOCFG2,   0x06},   // GDO2: sync-word match (was 0x0E carrier sense)
    {registers::IOCFG0,   0x00},   // GDO0: RX FIFO threshold
    {registers::FIFOTHR,  0x47},
    // SYNC = 0xAAAA — preamble master key; triggers on any FSK transmitter.
    // *** TEMPORARY reverse-engineering "wytrych" — replace with real PRIOS ***
    // *** sync word once it is recovered from hardware captures.            ***
    {registers::SYNC1,    0xAA},   // MASTER KEY — preamble pattern, NOT PRIOS sync
    {registers::SYNC0,    0xAA},   // MASTER KEY — preamble pattern, NOT PRIOS sync
    {registers::PKTLEN,   0xFF},
    {registers::PKTCTRL1, 0x00},
    {registers::PKTCTRL0, 0x02},
    {registers::FSCTRL1,  0x08},
    {registers::FSCTRL0,  0x00},
    {registers::FREQ2,    0x21},
    {registers::FREQ1,    0x6B},
    {registers::FREQ0,    0xD1},
    {registers::MDMCFG4,  0x5B},
    {registers::MDMCFG3,  0xF8},
    // MDMCFG2 = 0x02:
    //   MOD_FORMAT[6:4]=000 = 2-FSK
    //   MANCHESTER_EN[3]=0  = Manchester OFF
    //   SYNC_MODE[2:0]=010  = exact 16/16 bits → matches 0xAAAA preamble key
    {registers::MDMCFG2,  0x02},   // Preamble sniffer: 2-FSK, Manchester off, 16/16 sync
    {registers::MDMCFG1,  0x22},
    {registers::MDMCFG0,  0xF8},
    {registers::DEVIATN,  0x47},
    {registers::MCSM1,    0x3F},
    {registers::MCSM0,    0x18},
    {registers::FOCCFG,   0x1D},
    {registers::BSCFG,    0x1C},
    {registers::AGCCTRL2, 0xC7},
    {registers::AGCCTRL1, 0x00},
    {registers::AGCCTRL0, 0xB2},
    {registers::FREND1,   0xB6},
    {registers::FREND0,   0x10},
    {registers::FSCAL3,   0xEA},
    {registers::FSCAL2,   0x2A},
    {registers::FSCAL1,   0x00},
    {registers::FSCAL0,   0x1F},
    {registers::TEST2,    0x81},
    {registers::TEST1,    0x35},
    {registers::TEST0,    0x09},
};

static constexpr size_t kPriosR3DiscoveryConfigSize =
    sizeof(kPriosR3DiscoveryConfig) / sizeof(kPriosR3DiscoveryConfig[0]);

static constexpr PriosR3RegisterConfig kPriosR3DiscoveryConfigManchesterOn[] = {
    {registers::IOCFG2,   0x0E},
    {registers::IOCFG0,   0x00},
    {registers::FIFOTHR,  0x47},
    {registers::SYNC1,    0x54},
    {registers::SYNC0,    0x3D},
    {registers::PKTLEN,   0xFF},
    {registers::PKTCTRL1, 0x00},
    {registers::PKTCTRL0, 0x02},
    {registers::FSCTRL1,  0x08},
    {registers::FSCTRL0,  0x00},
    {registers::FREQ2,    0x21},
    {registers::FREQ1,    0x6B},
    {registers::FREQ0,    0xD1},
    {registers::MDMCFG4,  0x5B},
    {registers::MDMCFG3,  0xF8},
    {registers::MDMCFG2,  0x0C},   // Variant B discovery: Manchester on, no sync, carrier gated
    {registers::MDMCFG1,  0x22},
    {registers::MDMCFG0,  0xF8},
    {registers::DEVIATN,  0x47},
    {registers::MCSM1,    0x3F},
    {registers::MCSM0,    0x18},
    {registers::FOCCFG,   0x1D},
    {registers::BSCFG,    0x1C},
    {registers::AGCCTRL2, 0xC7},
    {registers::AGCCTRL1, 0x00},
    {registers::AGCCTRL0, 0xB2},
    {registers::FREND1,   0xB6},
    {registers::FREND0,   0x10},
    {registers::FSCAL3,   0xEA},
    {registers::FSCAL2,   0x2A},
    {registers::FSCAL1,   0x00},
    {registers::FSCAL0,   0x1F},
    {registers::TEST2,    0x81},
    {registers::TEST1,    0x35},
    {registers::TEST0,    0x09},
};

static constexpr size_t kPriosR3DiscoveryConfigManchesterOnSize =
    sizeof(kPriosR3DiscoveryConfigManchesterOn) /
    sizeof(kPriosR3DiscoveryConfigManchesterOn[0]);

// ---- Variant selector -------------------------------------------------------

// Returns the register config array and count for the requested variant.
// manchester_enabled=false → Variant A, manchester_enabled=true → Variant B.
inline const PriosR3RegisterConfig* prios_r3_profile(bool manchester_enabled,
                                                      size_t& out_count) {
    if (manchester_enabled) {
        out_count = kPriosR3ConfigManchesterOnSize;
        return kPriosR3ConfigManchesterOn;
    }
    out_count = kPriosR3ConfigSize;
    return kPriosR3Config;
}

inline const PriosR3RegisterConfig* prios_r3_discovery_profile(bool manchester_enabled,
                                                               size_t& out_count) {
    if (manchester_enabled) {
        out_count = kPriosR3DiscoveryConfigManchesterOnSize;
        return kPriosR3DiscoveryConfigManchesterOn;
    }
    out_count = kPriosR3DiscoveryConfigSize;
    return kPriosR3DiscoveryConfig;
}

} // namespace radio_cc1101
