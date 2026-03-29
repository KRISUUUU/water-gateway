#include "radio_cc1101/rx_frame_utils.hpp"

#include <algorithm>

namespace radio_cc1101 {

common::Result<RxFrameDrainPlan> plan_rx_frame_drain(uint8_t pkt_len, size_t max_data_size) {
    if (pkt_len == 0U || pkt_len >= max_data_size) {
        return common::Result<RxFrameDrainPlan>::error(common::ErrorCode::InvalidArgument);
    }

    RxFrameDrainPlan plan{};
    plan.frame_length = static_cast<uint16_t>(pkt_len + 1U);
    plan.bytes_after_length = static_cast<uint16_t>(pkt_len + 2U);
    return common::Result<RxFrameDrainPlan>::ok(plan);
}

size_t next_rx_burst_size(uint16_t remaining, uint8_t available, size_t max_burst_size) {
    return std::min<size_t>(std::min<size_t>(remaining, static_cast<size_t>(available)),
                            max_burst_size);
}

} // namespace radio_cc1101
