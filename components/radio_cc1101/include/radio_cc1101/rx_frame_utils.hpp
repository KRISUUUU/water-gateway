#pragma once

#include "common/result.hpp"

#include <cstddef>
#include <cstdint>

namespace radio_cc1101 {

struct RxFrameDrainPlan {
    uint16_t frame_length = 0;
    uint16_t bytes_after_length = 0;
};

common::Result<RxFrameDrainPlan> plan_rx_frame_drain(uint8_t pkt_len, size_t max_data_size);

size_t next_rx_burst_size(uint16_t remaining, uint8_t available, size_t max_burst_size);

} // namespace radio_cc1101
