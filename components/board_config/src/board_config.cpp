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

} // namespace board_config
