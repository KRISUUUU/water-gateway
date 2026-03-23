#pragma once

#include <string>

#include "common/result.hpp"
#include "radio_cc1101/radio_cc1101.hpp"
#include "wmbus_minimal_pipeline/wmbus_frame.hpp"

namespace wmbus_minimal_pipeline {

class WmbusPipeline {
public:
    static common::Result<WmbusFrame> from_radio_frame(
        const radio_cc1101::RawRadioFrame& input,
        common::TimestampMs timestamp_ms);

    static std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes);
};

}  // namespace wmbus_minimal_pipeline
