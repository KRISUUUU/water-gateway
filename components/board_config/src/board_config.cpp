#include "board_config/board_config.hpp"

namespace board_config {

radio_cc1101::SpiPins default_cc1101_pins() {
    return {
        .mosi = 23,
        .miso = 19,
        .sck = 18,
        .cs = 5,
        .gdo0 = 4,
        .gdo2 = 2,
    };
}

radio_cc1101::SpiBusConfig default_cc1101_spi_bus_config() {
    return {
        .host_id = 2,
        .clock_hz = 4 * 1000 * 1000,
        .max_transfer_size = 64,
    };
}

} // namespace board_config
