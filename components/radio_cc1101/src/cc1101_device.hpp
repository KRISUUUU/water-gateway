#pragma once

#include "common/result.hpp"
#include "radio_cc1101/cc1101_profile_tmode.hpp"
#include "radio_cc1101/radio_cc1101.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace radio_cc1101::device {

common::Result<void> apply_register_profile(void* spi_handle, RadioCounters& counters,
                                            const TmodeRegisterConfig* profile, size_t size);
common::Result<uint8_t> read_marcstate(void* spi_handle, RadioCounters& counters);
bool verify_chip_id(void* spi_handle, RadioCounters& counters, uint8_t& partnum, uint8_t& version);
int8_t convert_rssi(uint8_t raw_rssi);

} // namespace radio_cc1101::device
