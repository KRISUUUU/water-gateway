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

    if (record.total_bytes_captured < 11) {
        return result;
    }

    const uint8_t* b = record.captured_bytes;
    uint8_t L = b[0];

    // L must be at least 10 (C,M,M,A,A,A,A,V,T,CI)
    if (L < 10) {
        return result;
    }

    // C must be 0x44 (SND-NR) and CI must be 0xA2 (Mfct specific PRIOS)
    if (b[1] != 0x44 || b[10] != 0xA2) {
        return result;
    }

    result.valid = true;

    // Manufacturer ID (bytes 2-3, Little-endian)
    result.manufacturer_id = static_cast<uint16_t>(b[2] | (b[3] << 8));
    result.manufacturer[0] = static_cast<char>(((result.manufacturer_id >> 10) & 0x1F) + 64);
    result.manufacturer[1] = static_cast<char>(((result.manufacturer_id >> 5) & 0x1F) + 64);
    result.manufacturer[2] = static_cast<char>((result.manufacturer_id & 0x1F) + 64);
    result.manufacturer[3] = '\0';

    // Meter ID (bytes 4-7, Little-endian BCD)
    result.meter_id = b[4] | (b[5] << 8) | (b[6] << 16) | (b[7] << 24);
    std::snprintf(result.meter_key, sizeof(result.meter_key), "%02X%02X%02X%02X", b[7], b[6], b[5], b[4]);

    // Format A payload after byte 10 is encrypted
    result.encrypted = true;

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
