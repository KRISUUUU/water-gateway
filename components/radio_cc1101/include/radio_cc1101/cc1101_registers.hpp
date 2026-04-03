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
constexpr uint8_t FIFO_SIZE = 64;

} // namespace registers

} // namespace radio_cc1101
