#pragma once

#include "radio_cc1101/cc1101_profile_prios_r3.hpp"

#include <cstddef>

// PRIOS R4 CC1101 bring-up profiles.
//
// R4 reuses the verified PRIOS R3 framing assumptions and modem settings.
// The only known RF delta is carrier frequency:
//   - PRIOS R3: 868.95 MHz
//   - PRIOS R4: 868.30 MHz
//
// Sync word remains 0x1E9B and all other FSK parameters intentionally match
// the current R3 bring-up profiles so the existing bounded diagnostics,
// export, and identity-only pipeline can be reused without architectural
// changes.

namespace radio_cc1101 {

using PriosR4RegisterConfig = PriosR3RegisterConfig;

static constexpr PriosR4RegisterConfig kPriosR4Config[] = {
    {registers::IOCFG2,   0x06},
    {registers::IOCFG0,   0x00},
    {registers::FIFOTHR,  0x47},
    {registers::SYNC1,    0x1E},
    {registers::SYNC0,    0x9B},
    {registers::PKTLEN,   0xFF},
    {registers::PKTCTRL1, 0x00},
    {registers::PKTCTRL0, 0x02},
    {registers::FSCTRL1,  0x08},
    {registers::FSCTRL0,  0x00},
    // 868.30 MHz
    {registers::FREQ2,    0x21},
    {registers::FREQ1,    0x65},
    {registers::FREQ0,    0x6A},
    {registers::MDMCFG4,  0x5B},
    {registers::MDMCFG3,  0xF8},
    {registers::MDMCFG2,  0x02},
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

static constexpr size_t kPriosR4ConfigSize =
    sizeof(kPriosR4Config) / sizeof(kPriosR4Config[0]);

static constexpr PriosR4RegisterConfig kPriosR4ConfigManchesterOn[] = {
    {registers::IOCFG2,   0x06},
    {registers::IOCFG0,   0x00},
    {registers::FIFOTHR,  0x47},
    {registers::SYNC1,    0x1E},
    {registers::SYNC0,    0x9B},
    {registers::PKTLEN,   0xFF},
    {registers::PKTCTRL1, 0x00},
    {registers::PKTCTRL0, 0x02},
    {registers::FSCTRL1,  0x08},
    {registers::FSCTRL0,  0x00},
    {registers::FREQ2,    0x21},
    {registers::FREQ1,    0x65},
    {registers::FREQ0,    0x6A},
    {registers::MDMCFG4,  0x5B},
    {registers::MDMCFG3,  0xF8},
    {registers::MDMCFG2,  0x0A},
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

static constexpr size_t kPriosR4ConfigManchesterOnSize =
    sizeof(kPriosR4ConfigManchesterOn) / sizeof(kPriosR4ConfigManchesterOn[0]);

static constexpr PriosR4RegisterConfig kPriosR4DiscoveryConfig[] = {
    {registers::IOCFG2,   0x06},
    {registers::IOCFG0,   0x00},
    {registers::FIFOTHR,  0x47},
    {registers::SYNC1,    0xAA},
    {registers::SYNC0,    0xAA},
    {registers::PKTLEN,   0xFF},
    {registers::PKTCTRL1, 0x00},
    {registers::PKTCTRL0, 0x02},
    {registers::FSCTRL1,  0x08},
    {registers::FSCTRL0,  0x00},
    {registers::FREQ2,    0x21},
    {registers::FREQ1,    0x65},
    {registers::FREQ0,    0x6A},
    {registers::MDMCFG4,  0x5B},
    {registers::MDMCFG3,  0xF8},
    {registers::MDMCFG2,  0x02},
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

static constexpr size_t kPriosR4DiscoveryConfigSize =
    sizeof(kPriosR4DiscoveryConfig) / sizeof(kPriosR4DiscoveryConfig[0]);

static constexpr PriosR4RegisterConfig kPriosR4DiscoveryConfigManchesterOn[] = {
    {registers::IOCFG2,   0x0E},
    {registers::IOCFG0,   0x00},
    {registers::FIFOTHR,  0x47},
    {registers::SYNC1,    0x1E},
    {registers::SYNC0,    0x9B},
    {registers::PKTLEN,   0xFF},
    {registers::PKTCTRL1, 0x00},
    {registers::PKTCTRL0, 0x02},
    {registers::FSCTRL1,  0x08},
    {registers::FSCTRL0,  0x00},
    {registers::FREQ2,    0x21},
    {registers::FREQ1,    0x65},
    {registers::FREQ0,    0x6A},
    {registers::MDMCFG4,  0x5B},
    {registers::MDMCFG3,  0xF8},
    {registers::MDMCFG2,  0x0C},
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

static constexpr size_t kPriosR4DiscoveryConfigManchesterOnSize =
    sizeof(kPriosR4DiscoveryConfigManchesterOn) /
    sizeof(kPriosR4DiscoveryConfigManchesterOn[0]);

inline const PriosR4RegisterConfig* prios_r4_profile(bool manchester_enabled,
                                                     size_t& out_count) {
    if (manchester_enabled) {
        out_count = kPriosR4ConfigManchesterOnSize;
        return kPriosR4ConfigManchesterOn;
    }
    out_count = kPriosR4ConfigSize;
    return kPriosR4Config;
}

inline const PriosR4RegisterConfig* prios_r4_discovery_profile(bool manchester_enabled,
                                                               size_t& out_count) {
    if (manchester_enabled) {
        out_count = kPriosR4DiscoveryConfigManchesterOnSize;
        return kPriosR4DiscoveryConfigManchesterOn;
    }
    out_count = kPriosR4DiscoveryConfigSize;
    return kPriosR4DiscoveryConfig;
}

} // namespace radio_cc1101
