#include "wmbus_prios_rx/prios_decoder.hpp"
#include "wmbus_prios_rx/prios_analyzer.hpp"

#include <algorithm>
#include <cstring>

namespace wmbus_prios_rx {

namespace {
static constexpr char kHex[] = "0123456789ABCDEF";
}

PriosDecodedTelegram PriosDecoder::decode(const PriosCaptureRecord& record) {
    PriosDecodedTelegram result{};

    const auto fp = PriosAnalyzer::extract_fingerprint(record.captured_bytes,
                                                        record.total_bytes_captured);
    if (!fp.valid) {
        return result;  // valid = false
    }

    result.valid = true;

    // Format meter_key: 12 uppercase hex chars (6 fingerprint bytes).
    for (uint8_t i = 0; i < PriosDeviceFingerprint::kLength; ++i) {
        result.meter_key[i * 2u + 0u] = kHex[(fp.bytes[i] >> 4) & 0x0Fu];
        result.meter_key[i * 2u + 1u] = kHex[fp.bytes[i] & 0x0Fu];
    }
    result.meter_key[PriosDeviceFingerprint::kLength * 2u] = '\0';

    // Display prefix: first min(captured, kDisplayPrefixRawBytes) raw bytes as hex.
    const size_t raw_bytes = std::min<size_t>(record.total_bytes_captured,
                                              PriosDecodedTelegram::kDisplayPrefixRawBytes);
    for (size_t i = 0; i < raw_bytes; ++i) {
        result.display_prefix_hex[i * 2u + 0u] = kHex[(record.captured_bytes[i] >> 4) & 0x0Fu];
        result.display_prefix_hex[i * 2u + 1u] = kHex[record.captured_bytes[i] & 0x0Fu];
    }
    result.display_prefix_hex[raw_bytes * 2u] = '\0';
    result.display_prefix_length = static_cast<uint8_t>(raw_bytes);

    // Radio metadata.
    result.rssi_dbm          = record.rssi_dbm;
    result.lqi               = record.lqi;
    result.timestamp_ms      = record.timestamp_ms;
    result.sequence          = record.sequence;
    result.manchester_enabled = record.manchester_enabled;
    result.captured_length   = record.total_bytes_captured;

    return result;
}

} // namespace wmbus_prios_rx
