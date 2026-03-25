#pragma once

#include "radio_cc1101/radio_cc1101.hpp"

namespace board_config {

// Default CC1101 SPI/GDO pin mapping for the current hardware revision.
// Keeping board assumptions here avoids coupling orchestration code to wiring.
radio_cc1101::SpiPins default_cc1101_pins();
radio_cc1101::SpiBusConfig default_cc1101_spi_bus_config();

} // namespace board_config
