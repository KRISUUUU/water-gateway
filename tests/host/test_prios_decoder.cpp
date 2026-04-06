#include "host_test_stubs.hpp"
#include "wmbus_prios_rx/prios_decoder.hpp"
#include "wmbus_prios_rx/prios_capture_service.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace wmbus_prios_rx;

namespace {

// Build a PriosCaptureRecord with controlled content.
PriosCaptureRecord make_record(uint16_t length, uint8_t fill = 0x00) {
    PriosCaptureRecord r{};
    r.total_bytes_captured = length;
    r.sequence  = 1;
    r.rssi_dbm  = -72;
    r.lqi       = 90;
    r.timestamp_ms = 1712345678LL;
    r.manchester_enabled = false;
    for (uint16_t i = 0; i < length && i < PriosCaptureRecord::kMaxCaptureBytes; ++i) {
        r.captured_bytes[i] = fill;
    }
    return r;
}

// Build a record with a valid WMBus Format A PRIOS header.
PriosCaptureRecord make_record_with_header(const uint8_t meter_id[4], uint8_t filler = 0xAA) {
    PriosCaptureRecord r = make_record(20, filler);
    r.captured_bytes[0]  = 18;     // L
    r.captured_bytes[1]  = 0x44;   // C (SND-NR)
    r.captured_bytes[2]  = 0x4C;   // M (SAP)
    r.captured_bytes[3]  = 0x30;   // M
    r.captured_bytes[4]  = meter_id[0];
    r.captured_bytes[5]  = meter_id[1];
    r.captured_bytes[6]  = meter_id[2];
    r.captured_bytes[7]  = meter_id[3];
    r.captured_bytes[8]  = 0x01;   // Ver
    r.captured_bytes[9]  = 0x07;   // Type (Water)
    r.captured_bytes[10] = 0xA2;   // CI (PRIOS)
    return r;
}

// --------------------------------------------------------------------------

void test_decode_returns_invalid_for_short_capture() {
    auto r = make_record(10);
    r.captured_bytes[0] = 9; // L
    const auto decoded = PriosDecoder::decode(r);
    assert(!decoded.valid);
    printf("  PASS: decode returns invalid for capture < 11 bytes or L < 10\n");
}

void test_decode_returns_valid_at_least_11_bytes() {
    const uint8_t meter_id[4] = {0x78, 0x56, 0x34, 0x12};
    auto r = make_record_with_header(meter_id);
    r.total_bytes_captured = 11;
    const auto decoded = PriosDecoder::decode(r);
    assert(decoded.valid);
    printf("  PASS: decode returns valid at exactly 11 bytes (minimal header)\n");
}

void test_decode_meter_key_is_meter_id_bcds() {
    const uint8_t meter_id[4] = {0x78, 0x56, 0x34, 0x12};
    const auto r = make_record_with_header(meter_id);
    const auto decoded = PriosDecoder::decode(r);
    assert(decoded.valid);
    assert(std::strcmp(decoded.meter_key, "12345678") == 0);
    assert(decoded.meter_id == 0x12345678);
    assert(std::strcmp(decoded.manufacturer, "SAP") == 0);
    assert(decoded.encrypted == true);
    printf("  PASS: meter_key is meter ID BCDs parsed from bytes 4-7\n");
}

void test_decode_protocol_constants() {
    assert(std::strcmp(PriosDecodedTelegram::kProtocolName, "PRIOS_IZAR") == 0);
    printf("  PASS: protocol constants correct (PRIOS_IZAR)\n");
}

void test_decode_copies_radio_metadata() {
    const uint8_t meter_id[4] = {0x11, 0x22, 0x33, 0x44};
    PriosCaptureRecord r = make_record_with_header(meter_id);
    r.rssi_dbm          = -80;
    r.lqi               = 42;
    r.timestamp_ms      = 123456789LL;
    r.sequence          = 77;
    r.manchester_enabled = true;
    r.total_bytes_captured = 18;

    const auto decoded = PriosDecoder::decode(r);
    assert(decoded.valid);
    assert(decoded.rssi_dbm          == -80);
    assert(decoded.lqi               == 42);
    assert(decoded.timestamp_ms      == 123456789LL);
    assert(decoded.sequence          == 77);
    assert(decoded.manchester_enabled == true);
    assert(decoded.captured_length   == 18);
    printf("  PASS: radio metadata copied into decoded telegram\n");
}

void test_decode_display_prefix_hex_length_bounded_at_32_bytes() {
    PriosCaptureRecord r{};
    r.total_bytes_captured = PriosCaptureRecord::kMaxCaptureBytes;  // 64
    // Fill bytes 9-14 with known fingerprint, rest with 0xAA
    for (size_t i = 0; i < PriosCaptureRecord::kMaxCaptureBytes; ++i) {
        r.captured_bytes[i] = (i >= 9 && i < 15) ? static_cast<uint8_t>(i) : 0xAA;
    }

    const auto decoded = PriosDecoder::decode(r);
    assert(decoded.valid);
    // display_prefix must be exactly kDisplayPrefixRawBytes * 2 hex chars
    const size_t expected_hex_len = PriosDecodedTelegram::kDisplayPrefixRawBytes * 2u;
    assert(std::strlen(decoded.display_prefix_hex) == expected_hex_len);
    assert(decoded.display_prefix_length == PriosDecodedTelegram::kDisplayPrefixRawBytes);
    printf("  PASS: display_prefix_hex bounded at %zu raw bytes\n",
           PriosDecodedTelegram::kDisplayPrefixRawBytes);
}

void test_decode_display_prefix_hex_truncated_for_partial_capture() {
    const uint8_t meter_id[4] = {0x01, 0x02, 0x03, 0x04};
    PriosCaptureRecord r = make_record_with_header(meter_id);
    r.total_bytes_captured = 15;  // less than kDisplayPrefixRawBytes=32

    const auto decoded = PriosDecoder::decode(r);
    assert(decoded.valid);
    assert(decoded.display_prefix_length == 15);
    assert(std::strlen(decoded.display_prefix_hex) == 15u * 2u);
    printf("  PASS: display_prefix_hex truncated for partial capture\n");
}

void test_decode_display_prefix_first_byte_of_record() {
    const uint8_t meter_id[4] = {0x01, 0x02, 0x03, 0x04};
    PriosCaptureRecord r = make_record_with_header(meter_id);
    r.captured_bytes[0] = 0xDE; // Change length to DE (invalid as L but it's okay for display prefix checking? Wait, decode will return invalid if L < 10)
    // Actually if L = 0xDE, 0xDE >= 10, so validation will pass as long as total_bytes_captured is >= 11 and bytes[1]=0x44, bytes[10]=0xA2. Which they are.

    const auto decoded = PriosDecoder::decode(r);
    assert(decoded.valid);
    assert(decoded.display_prefix_hex[0] == 'D');
    assert(decoded.display_prefix_hex[1] == 'E');
    printf("  PASS: display_prefix_hex starts with first captured byte\n");
}

} // namespace

int main() {
    printf("=== test_prios_decoder ===\n");
    test_decode_returns_invalid_for_short_capture();
    test_decode_returns_valid_at_least_11_bytes();
    test_decode_meter_key_is_meter_id_bcds();
    test_decode_protocol_constants();
    test_decode_copies_radio_metadata();
    test_decode_display_prefix_hex_length_bounded_at_32_bytes();
    test_decode_display_prefix_hex_truncated_for_partial_capture();
    test_decode_display_prefix_first_byte_of_record();
    printf("All PRIOS decoder tests passed.\n");
    return 0;
}
