#include "wmbus_prios_rx/prios_export.hpp"

#include "wmbus_prios_rx/prios_analyzer.hpp"
#include "wmbus_prios_rx/prios_fixture.hpp"

namespace wmbus_prios_rx {

namespace {

constexpr char kHex[] = "0123456789ABCDEF";

uint8_t bounded_capture_length(const PriosCaptureRecord& record) {
    return record.total_bytes_captured < PriosFixtureFrame::kMaxBytes
               ? static_cast<uint8_t>(record.total_bytes_captured)
               : static_cast<uint8_t>(PriosFixtureFrame::kMaxBytes);
}

void append_hex_string(std::string& out, const uint8_t* bytes, uint8_t length) {
    out.push_back('"');
    for (uint8_t i = 0; i < length; ++i) {
        out.push_back(kHex[(bytes[i] >> 4) & 0x0F]);
        out.push_back(kHex[bytes[i] & 0x0F]);
    }
    out.push_back('"');
}

} // namespace

void append_export_json_object(std::string& out, const PriosCaptureRecord& record) {
    const PriosFixtureFrame frame = PriosFixtureFrame::from_record(record);
    const uint8_t preview_length =
        frame.length < PriosCaptureRecord::kDisplayPrefixBytes
            ? frame.length
            : static_cast<uint8_t>(PriosCaptureRecord::kDisplayPrefixBytes);

    out.clear();
    out.reserve(384 + static_cast<size_t>(frame.length) * 6U);

    out += "{\"total_bytes_captured\":";
    out += std::to_string(static_cast<unsigned int>(record.total_bytes_captured));
    out += ",\"length\":";
    out += std::to_string(static_cast<unsigned int>(frame.length));
    out += ",\"rssi_dbm\":";
    out += std::to_string(static_cast<int>(frame.rssi_dbm));
    out += ",\"lqi\":";
    out += std::to_string(static_cast<unsigned int>(frame.lqi));
    out += ",\"radio_crc_ok\":";
    out += frame.radio_crc_ok ? "true" : "false";
    out += ",\"radio_crc_available\":";
    out += frame.radio_crc_available ? "true" : "false";
    out += ",\"timestamp_ms\":";
    out += std::to_string(static_cast<long long>(frame.timestamp_ms));
    out += ",\"variant\":\"";
    out += record.manchester_enabled ? "manchester_on" : "manchester_off";
    out += "\",\"device_fingerprint\":";
    {
        const auto fp = PriosAnalyzer::extract_fingerprint(frame.bytes, frame.length);
        if (fp.valid) {
            out.push_back('"');
            for (uint8_t i = 0; i < PriosDeviceFingerprint::kLength; ++i) {
                out.push_back(kHex[(fp.bytes[i] >> 4) & 0x0F]);
                out.push_back(kHex[fp.bytes[i] & 0x0F]);
            }
            out.push_back('"');
        } else {
            out += "null";
        }
    }
    out += ",\"display_prefix_hex\":";
    append_hex_string(out, frame.bytes, preview_length);
    out += ",\"full_hex\":";
    append_hex_string(out, frame.bytes, bounded_capture_length(record));
    out += ",\"hex\":";
    append_hex_string(out, frame.bytes, bounded_capture_length(record));
    out.push_back('}');
}

} // namespace wmbus_prios_rx
