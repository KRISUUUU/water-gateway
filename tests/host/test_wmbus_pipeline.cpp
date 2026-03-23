#include <cassert>

#include "radio_cc1101/radio_cc1101.hpp"
#include "wmbus_minimal_pipeline/wmbus_pipeline.hpp"

int main() {
    radio_cc1101::RawRadioFrame raw{};
    raw.bytes = {0x01, 0xAB, 0xFF};
    raw.rssi = -70;
    raw.lqi = 90;
    raw.crc_ok = true;

    const auto result = wmbus_minimal_pipeline::WmbusPipeline::from_radio_frame(raw, 123456);
    assert(result.ok());

    const auto& frame = result.value();
    assert(frame.raw_hex == "01ABFF");
    assert(frame.metadata.timestamp_ms == 123456);
    assert(frame.metadata.length == 3);
    assert(frame.metadata.crc_ok == true);

    return 0;
}
