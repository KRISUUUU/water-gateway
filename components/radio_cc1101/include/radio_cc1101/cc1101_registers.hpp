#pragma once

#include <cstdint>

namespace radio_cc1101::registers {

inline constexpr std::uint8_t IOCFG2 = 0x00;
inline constexpr std::uint8_t IOCFG1 = 0x01;
inline constexpr std::uint8_t IOCFG0 = 0x02;
inline constexpr std::uint8_t FIFOTHR = 0x03;
inline constexpr std::uint8_t SYNC1 = 0x04;
inline constexpr std::uint8_t SYNC0 = 0x05;
inline constexpr std::uint8_t PKTLEN = 0x06;
inline constexpr std::uint8_t PKTCTRL1 = 0x07;
inline constexpr std::uint8_t PKTCTRL0 = 0x08;
inline constexpr std::uint8_t ADDR = 0x09;
inline constexpr std::uint8_t CHANNR = 0x0A;
inline constexpr std::uint8_t FSCTRL1 = 0x0B;
inline constexpr std::uint8_t FSCTRL0 = 0x0C;
inline constexpr std::uint8_t FREQ2 = 0x0D;
inline constexpr std::uint8_t FREQ1 = 0x0E;
inline constexpr std::uint8_t FREQ0 = 0x0F;
inline constexpr std::uint8_t MDMCFG4 = 0x10;
inline constexpr std::uint8_t MDMCFG3 = 0x11;
inline constexpr std::uint8_t MDMCFG2 = 0x12;
inline constexpr std::uint8_t MDMCFG1 = 0x13;
inline constexpr std::uint8_t MDMCFG0 = 0x14;
inline constexpr std::uint8_t DEVIATN = 0x15;
inline constexpr std::uint8_t MCSM2 = 0x16;
inline constexpr std::uint8_t MCSM1 = 0x17;
inline constexpr std::uint8_t MCSM0 = 0x18;
inline constexpr std::uint8_t FOCCFG = 0x19;
inline constexpr std::uint8_t BSCFG = 0x1A;
inline constexpr std::uint8_t AGCCTRL2 = 0x1B;
inline constexpr std::uint8_t AGCCTRL1 = 0x1C;
inline constexpr std::uint8_t AGCCTRL0 = 0x1D;
inline constexpr std::uint8_t FREND1 = 0x21;
inline constexpr std::uint8_t FREND0 = 0x22;
inline constexpr std::uint8_t FSCAL3 = 0x23;
inline constexpr std::uint8_t FSCAL2 = 0x24;
inline constexpr std::uint8_t FSCAL1 = 0x25;
inline constexpr std::uint8_t FSCAL0 = 0x26;
inline constexpr std::uint8_t TEST2 = 0x2C;
inline constexpr std::uint8_t TEST1 = 0x2D;
inline constexpr std::uint8_t TEST0 = 0x2E;

inline constexpr std::uint8_t SRES = 0x30;
inline constexpr std::uint8_t SFSTXON = 0x31;
inline constexpr std::uint8_t SXOFF = 0x32;
inline constexpr std::uint8_t SCAL = 0x33;
inline constexpr std::uint8_t SRX = 0x34;
inline constexpr std::uint8_t STX = 0x35;
inline constexpr std::uint8_t SIDLE = 0x36;
inline constexpr std::uint8_t SAFC = 0x37;
inline constexpr std::uint8_t SWOR = 0x38;
inline constexpr std::uint8_t SPWD = 0x39;
inline constexpr std::uint8_t SFRX = 0x3A;
inline constexpr std::uint8_t SFTX = 0x3B;
inline constexpr std::uint8_t SWORRST = 0x3C;
inline constexpr std::uint8_t SNOP = 0x3D;

}  // namespace radio_cc1101::registers
