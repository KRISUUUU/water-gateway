#include "wmbus_prios_rx/prios_decoder.hpp"
#include "wmbus_prios_rx/prios_analyzer.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace wmbus_prios_rx {

namespace {
static constexpr char kHex[] = "0123456789ABCDEF";
}

bool PriosDecoder::is_likely_prios(const PriosCaptureRecord& record) {
    return record.total_bytes_captured >= 13 &&
           record.captured_bytes[12] == 0xA2;
}

PriosDecodedTelegram PriosDecoder::decode(const PriosCaptureRecord& record) {
    PriosDecodedTelegram result{};

    if (!is_likely_prios(record)) {
        result.valid = false;
        return result;
    }

    const uint8_t* b = record.captured_bytes;
    result.valid = true;
    result.radio_profile = record.radio_profile;

    // Manufacturer ID (SAP = Diehl)
    result.manufacturer_id = static_cast<uint16_t>(b[2] | (b[3] << 8));
    result.manufacturer[0] = static_cast<char>(((result.manufacturer_id >> 10) & 0x1F) + 64);
    result.manufacturer[1] = static_cast<char>(((result.manufacturer_id >> 5) & 0x1F) + 64);
    result.manufacturer[2] = static_cast<char>((result.manufacturer_id & 0x1F) + 64);
    result.manufacturer[3] = '\0';

    // Meter ID (Serial Number) - 4 bajty little-endian z bajtów 4-7
    result.meter_id = b[4] | (b[5] << 8) | (b[6] << 16) | (b[7] << 24);
    std::snprintf(result.meter_key, sizeof(result.meter_key), "%02X%02X%02X%02X", b[7], b[6], b[5], b[4]);

    result.encrypted = true;   // na razie zawsze (zgodnie z Prompt 7)

    // Display prefix hex (do pokazania w Live Telegrams)
    const size_t raw_bytes = std::min<size_t>(record.total_bytes_captured, PriosDecodedTelegram::kDisplayPrefixRawBytes);
    for (size_t i = 0; i < raw_bytes; ++i) {
        result.display_prefix_hex[i * 2]     = kHex[(record.captured_bytes[i] >> 4) & 0x0F];
        result.display_prefix_hex[i * 2 + 1] = kHex[record.captured_bytes[i] & 0x0F];
    }
    result.display_prefix_hex[raw_bytes * 2] = '\0';
    result.display_prefix_length = static_cast<uint8_t>(raw_bytes);

    // Metadata
    result.rssi_dbm = record.rssi_dbm;
    result.lqi = record.lqi;
    result.timestamp_ms = record.timestamp_ms;
    result.sequence = record.sequence;
    result.manchester_enabled = record.manchester_enabled;
    result.captured_length = record.total_bytes_captured;

    return result;
}

} // namespace wmbus_prios_rx
