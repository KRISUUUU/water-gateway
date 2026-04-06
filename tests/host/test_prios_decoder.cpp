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

// Build a record with a specific fingerprint at bytes 9-14.
PriosCaptureRecord make_record_with_fp(const uint8_t fp[6], uint8_t filler = 0xAA) {
    PriosCaptureRecord r{};
    r.total_bytes_captured = 20;
    r.sequence   = 42;
    r.rssi_dbm   = -65;
    r.lqi        = 95;
    r.timestamp_ms = 9000000LL;
    r.manchester_enabled = true;
    for (uint8_t i = 0; i < 20; ++i) { r.captured_bytes[i] = filler; }
    for (uint8_t i = 0; i < 6; ++i) {
        r.captured_bytes[PriosDeviceFingerprint::kOffset + i] = fp[i];
    }
    return r;
}

// --------------------------------------------------------------------------

void test_decode_returns_invalid_for_short_capture() {
    const auto r = make_record(14);  // kOffset+kLength=15, need >= 15
    const auto decoded = PriosDecoder::decode(r);
    assert(!decoded.valid);
    printf("  PASS: decode returns invalid for capture < 15 bytes\n");
}

void test_decode_returns_valid_at_exactly_15_bytes() {
    const uint8_t fp[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    auto r = make_record_with_fp(fp);
    r.total_bytes_captured = 15;
    const auto decoded = PriosDecoder::decode(r);
    assert(decoded.valid);
    printf("  PASS: decode returns valid at exactly 15 bytes\n");
}

void test_decode_meter_key_is_fingerprint_hex() {
    const uint8_t fp[6] = {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45};
    const auto r = make_record_with_fp(fp);
    const auto decoded = PriosDecoder::decode(r);
    assert(decoded.valid);
    assert(std::strcmp(decoded.meter_key, "ABCDEF012345") == 0);
    printf("  PASS: meter_key is fingerprint bytes as uppercase hex\n");
}

void test_decode_protocol_constants() {
    assert(std::strcmp(PriosDecodedTelegram::kProtocolName, "PRIOS_R3") == 0);
    assert(std::strcmp(PriosDecodedTelegram::kVendor, "Techem") == 0);
    assert(PriosDecodedTelegram::kManufacturerId == 0x5068u);
    printf("  PASS: protocol constants correct (PRIOS_R3, Techem, 0x5068)\n");
}

void test_decode_copies_radio_metadata() {
    const uint8_t fp[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    PriosCaptureRecord r = make_record_with_fp(fp);
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
    const uint8_t fp[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    PriosCaptureRecord r = make_record_with_fp(fp);
    r.total_bytes_captured = 15;  // less than kDisplayPrefixRawBytes=32

    const auto decoded = PriosDecoder::decode(r);
    assert(decoded.valid);
    assert(decoded.display_prefix_length == 15);
    assert(std::strlen(decoded.display_prefix_hex) == 15u * 2u);
    printf("  PASS: display_prefix_hex truncated for partial capture\n");
}

void test_decode_display_prefix_first_byte_of_record() {
    // record.captured_bytes[0] = 0xDE, check first 2 chars of display_prefix_hex
    const uint8_t fp[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    PriosCaptureRecord r = make_record_with_fp(fp);
    r.captured_bytes[0] = 0xDE;

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
    test_decode_returns_valid_at_exactly_15_bytes();
    test_decode_meter_key_is_fingerprint_hex();
    test_decode_protocol_constants();
    test_decode_copies_radio_metadata();
    test_decode_display_prefix_hex_length_bounded_at_32_bytes();
    test_decode_display_prefix_hex_truncated_for_partial_capture();
    test_decode_display_prefix_first_byte_of_record();
    printf("All PRIOS decoder tests passed.\n");
    return 0;
}
