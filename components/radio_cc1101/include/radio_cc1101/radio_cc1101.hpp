#pragma once

#include <cstdint>
#include <vector>

#include "common/result.hpp"

namespace radio_cc1101 {

enum class RadioState : std::uint8_t {
    Uninitialized = 0,
    Idle,
    Calibrating,
    Receiving,
    Error
};

struct RadioCounters {
    std::uint32_t resets{0};
    std::uint32_t calibrations{0};
    std::uint32_t rx_entries{0};
    std::uint32_t fifo_overflows{0};
    std::uint32_t recoveries{0};
    std::uint32_t frames_seen{0};
    std::uint32_t read_errors{0};
};

struct RadioStatus {
    RadioState state{RadioState::Uninitialized};
    RadioCounters counters{};
    int last_rssi{0};
    std::uint8_t last_lqi{0};
};

struct RawRadioFrame {
    std::vector<std::uint8_t> bytes{};
    int rssi{0};
    std::uint8_t lqi{0};
    bool crc_ok{false};
};

class RadioCc1101 {
public:
    static RadioCc1101& instance();

    common::Result<void> initialize();
    common::Result<void> reset();
    common::Result<void> enter_rx_mode();
    common::Result<void> recover_from_rx_error();
    common::Result<RawRadioFrame> read_frame();

    [[nodiscard]] RadioStatus status() const;

private:
    RadioCc1101() = default;

    bool initialized_{false};
    RadioStatus status_{};
};

}  // namespace radio_cc1101
