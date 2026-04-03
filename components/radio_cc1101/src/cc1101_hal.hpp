#pragma once

#include "common/result.hpp"
#include "radio_cc1101/radio_cc1101.hpp"

#include <cstddef>
#include <cstdint>

namespace radio_cc1101::hal {

common::Result<void> initialize_spi_device(void*& spi_handle, const SpiPins& pins,
                                           const SpiBusConfig& bus_config);
void deinitialize_spi_device(void*& spi_handle, int host_id);
common::Result<void> configure_input_pin(int pin);

bool spi_strobe(void* spi_handle, uint8_t strobe_addr, uint8_t* chip_status = nullptr);
bool spi_read_register(void* spi_handle, uint8_t addr, uint8_t& value);
bool spi_write_register(void* spi_handle, uint8_t addr, uint8_t value);
bool spi_read_burst(void* spi_handle, uint8_t addr, uint8_t* buffer, size_t length);

} // namespace radio_cc1101::hal
