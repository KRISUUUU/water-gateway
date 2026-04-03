#pragma once

#include "radio_cc1101/cc1101_registers.hpp"

#include <cstddef>
#include <cstdint>

namespace radio_cc1101 {

struct TmodeRegisterConfig {
    uint8_t addr;
    uint8_t value;
};

// T-mode: 868.95 MHz, ~32.768 kbaud raw capture profile for post-capture 3-of-6 processing.
static constexpr TmodeRegisterConfig kTmodeConfig[] = {
    {registers::IOCFG2, 0x06},   // GDO2: sync word sent/received
    {registers::IOCFG0, 0x00},   // GDO0: packet sync/clock source routed to owner-task IRQ plumbing
    {registers::FIFOTHR, 0x47},  // RX FIFO threshold: 33 bytes
    {registers::SYNC1, 0x54},    // WMBus T-mode sync word MSB
    {registers::SYNC0, 0x3D},    // WMBus T-mode sync word LSB
    {registers::PKTLEN, 0xFF},   // Upper bound retained for FIFO protection
    {registers::PKTCTRL1, 0x00}, // No address filtering, no appended status bytes in RX FIFO
    {registers::PKTCTRL0, 0x02}, // Infinite packet length, whitening disabled, CRC disabled
    {registers::FSCTRL1, 0x08},  // IF frequency
    {registers::FSCTRL0, 0x00},  // Frequency offset
    {registers::FREQ2, 0x21},
    {registers::FREQ1, 0x6B},
    {registers::FREQ0, 0xD1},
    {registers::MDMCFG4, 0x5B},  // Channel BW ~325 kHz, DRATE_E=11
    {registers::MDMCFG3, 0xF8},  // DRATE_M=248 -> ~32.768 kbaud
    {registers::MDMCFG2, 0x03},  // 30/32 sync, Manchester disabled
    {registers::MDMCFG1, 0x22},  // 4 preamble bytes, FEC disabled
    {registers::MDMCFG0, 0xF8},  // Channel spacing
    {registers::DEVIATN, 0x47},  // Deviation ~47.607 kHz
    {registers::MCSM1, 0x3F},    // Stay in RX after RX/TX
    {registers::MCSM0, 0x18},    // Autocal on idle-to-RX/TX
    {registers::FOCCFG, 0x1D},
    {registers::BSCFG, 0x1C},
    {registers::AGCCTRL2, 0xC7},
    {registers::AGCCTRL1, 0x00},
    {registers::AGCCTRL0, 0xB2},
    {registers::FREND1, 0xB6},
    {registers::FREND0, 0x10},
    {registers::FSCAL3, 0xEA},
    {registers::FSCAL2, 0x2A},
    {registers::FSCAL1, 0x00},
    {registers::FSCAL0, 0x1F},
    {registers::TEST2, 0x81},
    {registers::TEST1, 0x35},
    {registers::TEST0, 0x09},
};

static constexpr size_t kTmodeConfigSize = sizeof(kTmodeConfig) / sizeof(kTmodeConfig[0]);

constexpr const TmodeRegisterConfig* find_tmode_register_config(uint8_t addr) {
    for (size_t i = 0; i < kTmodeConfigSize; ++i) {
        if (kTmodeConfig[i].addr == addr) {
            return &kTmodeConfig[i];
        }
    }
    return nullptr;
}

constexpr bool tmode_config_contains(uint8_t addr, uint8_t value) {
    const auto* entry = find_tmode_register_config(addr);
    return entry != nullptr && entry->value == value;
}

} // namespace radio_cc1101
