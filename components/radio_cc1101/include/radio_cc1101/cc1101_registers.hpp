#pragma once

#include <cstdint>

namespace radio_cc1101 {
namespace registers {

// CC1101 Configuration Register Addresses (write: addr, read: addr | 0x80)
constexpr uint8_t IOCFG2 = 0x00;
constexpr uint8_t IOCFG1 = 0x01;
constexpr uint8_t IOCFG0 = 0x02;
constexpr uint8_t FIFOTHR = 0x03;
constexpr uint8_t SYNC1 = 0x04;
constexpr uint8_t SYNC0 = 0x05;
constexpr uint8_t PKTLEN = 0x06;
constexpr uint8_t PKTCTRL1 = 0x07;
constexpr uint8_t PKTCTRL0 = 0x08;
constexpr uint8_t ADDR = 0x09;
constexpr uint8_t CHANNR = 0x0A;
constexpr uint8_t FSCTRL1 = 0x0B;
constexpr uint8_t FSCTRL0 = 0x0C;
constexpr uint8_t FREQ2 = 0x0D;
constexpr uint8_t FREQ1 = 0x0E;
constexpr uint8_t FREQ0 = 0x0F;
constexpr uint8_t MDMCFG4 = 0x10;
constexpr uint8_t MDMCFG3 = 0x11;
constexpr uint8_t MDMCFG2 = 0x12;
constexpr uint8_t MDMCFG1 = 0x13;
constexpr uint8_t MDMCFG0 = 0x14;
constexpr uint8_t DEVIATN = 0x15;
constexpr uint8_t MCSM2 = 0x16;
constexpr uint8_t MCSM1 = 0x17;
constexpr uint8_t MCSM0 = 0x18;
constexpr uint8_t FOCCFG = 0x19;
constexpr uint8_t BSCFG = 0x1A;
constexpr uint8_t AGCCTRL2 = 0x1B;
constexpr uint8_t AGCCTRL1 = 0x1C;
constexpr uint8_t AGCCTRL0 = 0x1D;
constexpr uint8_t WOREVT1 = 0x1E;
constexpr uint8_t WOREVT0 = 0x1F;
constexpr uint8_t WORCTRL = 0x20;
constexpr uint8_t FREND1 = 0x21;
constexpr uint8_t FREND0 = 0x22;
constexpr uint8_t FSCAL3 = 0x23;
constexpr uint8_t FSCAL2 = 0x24;
constexpr uint8_t FSCAL1 = 0x25;
constexpr uint8_t FSCAL0 = 0x26;
constexpr uint8_t RCCTRL1 = 0x27;
constexpr uint8_t RCCTRL0 = 0x28;
constexpr uint8_t FSTEST = 0x29;
constexpr uint8_t PTEST = 0x2A;
constexpr uint8_t AGCTEST = 0x2B;
constexpr uint8_t TEST2 = 0x2C;
constexpr uint8_t TEST1 = 0x2D;
constexpr uint8_t TEST0 = 0x2E;

// Command Strobe Registers
constexpr uint8_t SRES = 0x30;
constexpr uint8_t SFSTXON = 0x31;
constexpr uint8_t SXOFF = 0x32;
constexpr uint8_t SCAL = 0x33;
constexpr uint8_t SRX = 0x34;
constexpr uint8_t STX = 0x35;
constexpr uint8_t SIDLE = 0x36;
constexpr uint8_t SWOR = 0x38;
constexpr uint8_t SPWD = 0x39;
constexpr uint8_t SFRX = 0x3A;
constexpr uint8_t SFTX = 0x3B;
constexpr uint8_t SWORRST = 0x3C;
constexpr uint8_t SNOP = 0x3D;

// Status Registers (burst read: addr | 0xC0)
constexpr uint8_t PARTNUM = 0x30;
constexpr uint8_t VERSION = 0x31;
constexpr uint8_t FREQEST = 0x32;
constexpr uint8_t LQI_REG = 0x33;
constexpr uint8_t RSSI_REG = 0x34;
constexpr uint8_t MARCSTATE = 0x35;
constexpr uint8_t WORTIME1 = 0x36;
constexpr uint8_t WORTIME0 = 0x37;
constexpr uint8_t PKTSTATUS = 0x38;
constexpr uint8_t VCO_VC_DAC = 0x39;
constexpr uint8_t TXBYTES = 0x3A;
constexpr uint8_t RXBYTES = 0x3B;
constexpr uint8_t RCCTRL1_STATUS = 0x3C;
constexpr uint8_t RCCTRL0_STATUS = 0x3D;

// FIFO access
constexpr uint8_t FIFO_TX = 0x3F;
constexpr uint8_t FIFO_RX = 0x3F;

// SPI header byte modifiers
constexpr uint8_t READ_SINGLE = 0x80;
constexpr uint8_t READ_BURST = 0xC0;
constexpr uint8_t WRITE_BURST = 0x40;

// MARCSTATE values
constexpr uint8_t MARCSTATE_IDLE = 0x01;
constexpr uint8_t MARCSTATE_RX = 0x0D;
constexpr uint8_t MARCSTATE_RXFIFO_OVERFLOW = 0x11;

// Chip ID expected from PARTNUM
constexpr uint8_t CC1101_PARTNUM = 0x00;
constexpr uint8_t CC1101_VERSION_EXPECTED = 0x14;

// Maximum FIFO size
constexpr size_t FIFO_SIZE = 64;

// Maximum WMBus T-mode frame size (including preamble overhead)
constexpr size_t MAX_FRAME_SIZE = 290;

} // namespace registers

// Register configuration for Wireless M-Bus T-mode (868.95 MHz, 100 kbps, 3-of-6)
// Based on known-good configurations from open-source WMBus receiver projects
// (rtl-wmbus, wmbusmeters CC1101 configs, CULFW wmbus config).
//
// IMPORTANT: These values are best-effort from datasheet calculations and
// community configurations. Real hardware testing is required to validate.
struct TmodeRegisterConfig {
    uint8_t addr;
    uint8_t value;
};

// T-mode: 868.95 MHz, 32.768 kbaud Manchester (effectively 100 kbps 3-of-6)
// The CC1101 is configured for asynchronous serial mode, as the T-mode
// 3-of-6 encoding is typically decoded in software post-capture.
static constexpr TmodeRegisterConfig kTmodeConfig[] = {
    {registers::IOCFG2, 0x06},   // GDO2: sync word sent/received
    {registers::IOCFG0, 0x00},   // GDO0: CLK_XOSC/192 (not used, low)
    {registers::FIFOTHR, 0x47},  // RX FIFO threshold: 33 bytes
    {registers::PKTLEN, 0xFF},   // Max packet length 255
    {registers::PKTCTRL1, 0x04}, // Append status (RSSI/LQI/CRC) to RX FIFO
    {registers::PKTCTRL0, 0x00}, // Fixed packet length mode, no CRC autoflush
    {registers::FSCTRL1, 0x08},  // IF frequency
    {registers::FSCTRL0, 0x00},  // Frequency offset
    // 868.95 MHz: FREQ = 868.95 * 2^16 / 26 = 0x2188CA
    {registers::FREQ2, 0x21},
    {registers::FREQ1, 0x88},
    {registers::FREQ0, 0xCA},
    // T-mode data rate: ~32.768 kbaud (Manchester)
    {registers::MDMCFG4, 0x5B},  // Channel BW ~325 kHz, DRATE_E=11
    {registers::MDMCFG3, 0xF8},  // DRATE_M=248 → ~32.768 kbaud
    {registers::MDMCFG2, 0x03},  // 30/32 sync, Manchester disabled (raw capture)
    {registers::MDMCFG1, 0x22},  // 4 preamble bytes, FEC disabled
    {registers::MDMCFG0, 0xF8},  // Channel spacing
    {registers::DEVIATN, 0x47},  // Deviation ~47.607 kHz
    {registers::MCSM1, 0x3F},    // CCA mode always, stay in RX after RX/TX
    {registers::MCSM0, 0x18},    // Autocal on idle-to-RX/TX, PO_TIMEOUT
    {registers::FOCCFG, 0x1D},   // Freq offset compensation
    {registers::BSCFG, 0x1C},    // Bit sync config
    {registers::AGCCTRL2, 0xC7}, // AGC: max DVGA gain, max LNA gain
    {registers::AGCCTRL1, 0x00}, // AGC LNA priority
    {registers::AGCCTRL0, 0xB2}, // AGC: 3dB decision boundary, filters
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

} // namespace radio_cc1101
