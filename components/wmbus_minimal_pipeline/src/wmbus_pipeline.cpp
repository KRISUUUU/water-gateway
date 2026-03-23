#include "wmbus_minimal_pipeline/wmbus_pipeline.hpp"

#include <iomanip>
#include <sstream>

namespace wmbus_minimal_pipeline {

common::Result<WmbusFrame> WmbusPipeline::from_radio_frame(
    const radio_cc1101::RawRadioFrame& input,
    common::TimestampMs timestamp_ms) {
    WmbusFrame frame{};
    frame.raw_bytes = input.bytes;
    frame.raw_hex = bytes_to_hex(input.bytes);
    frame.metadata.timestamp_ms = timestamp_ms;
    frame.metadata.rssi = input.rssi;
    frame.metadata.lqi = input.lqi;
    frame.metadata.crc_ok = input.crc_ok;
    frame.metadata.length = input.bytes.size();

    return common::Result<WmbusFrame>(frame);
}

std::string WmbusPipeline::bytes_to_hex(const std::vector<std::uint8_t>& bytes) {
    std::ostringstream oss;
    oss << std::hex << std::uppercase << std::setfill('0');

    for (const auto byte : bytes) {
        oss << std::setw(2) << static_cast<int>(byte);
    }

    return oss.str();
}

}  // namespace wmbus_minimal_pipeline
