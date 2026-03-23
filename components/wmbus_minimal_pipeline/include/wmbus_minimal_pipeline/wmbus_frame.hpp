#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/types.hpp"

namespace wmbus_minimal_pipeline {

struct WmbusFrameMetadata {
    common::TimestampMs timestamp_ms{0};
    int rssi{0};
    std::uint8_t lqi{0};
    bool crc_ok{false};
    std::size_t length{0};
};

struct WmbusFrame {
    std::vector<std::uint8_t> raw_bytes{};
    std::string raw_hex{};
    WmbusFrameMetadata metadata{};
};

}  // namespace wmbus_minimal_pipeline
