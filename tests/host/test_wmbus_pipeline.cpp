#include "host_test_stubs.hpp"
#include "radio_cc1101/radio_cc1101.hpp"
#include "wmbus_minimal_pipeline/wmbus_frame.hpp"
#include "wmbus_minimal_pipeline/wmbus_pipeline.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace wmbus_minimal_pipeline;
using namespace radio_cc1101;

// Reverse of decode_3of6_symbol (rtl_433 Mode-T table): nibble -> 6-bit codeword 0..63.
static constexpr uint8_t kEnc3of6[16] = {22, 13, 14, 11, 28, 25, 26, 19, 44, 37, 38, 35, 52, 49, 50, 41};

static void encode_3of6_link_layer(const uint8_t* link, size_t link_len, uint8_t* out_symbols) {
    for (size_t i = 0; i < link_len; ++i) {
        out_symbols[i * 2U] = kEnc3of6[(link[i] >> 4) & 0x0FU];
        out_symbols[i * 2U + 1U] = kEnc3of6[link[i] & 0x0FU];
    }
}

// 12-byte Format A link layer with valid DLL CRC (bytes 0-9), CRC at 10-11 (big-endian storage).
static const uint8_t kValidLink12[12] = {
    0x0B, 0x44, 0x93, 0x15, 0x78, 0x56, 0x34, 0x12, 0x34, 0x12, 0x3E, 0xBF,
};

static void test_bytes_to_hex_basic() {
    uint8_t data[] = {0x2C, 0x44, 0x93, 0x15};
    auto hex = WmbusPipeline::bytes_to_hex(data, 4);
    assert(hex == "2C449315");
    printf("  PASS: bytes_to_hex basic\n");
}

static void test_bytes_to_hex_empty() {
    auto hex = WmbusPipeline::bytes_to_hex(nullptr, 0);
    assert(hex.empty());
    printf("  PASS: bytes_to_hex empty\n");
}

static void test_bytes_to_hex_single() {
    uint8_t data[] = {0xFF};
    auto hex = WmbusPipeline::bytes_to_hex(data, 1);
    assert(hex == "FF");
    printf("  PASS: bytes_to_hex single byte\n");
}

static void test_hex_to_bytes() {
    uint8_t out[4] = {};
    const size_t n = WmbusPipeline::hex_to_bytes("2C449315", out, 4);
    assert(n == 4);
    (void)n;
    assert(out[0] == 0x2C);
    assert(out[1] == 0x44);
    assert(out[2] == 0x93);
    assert(out[3] == 0x15);
    printf("  PASS: hex_to_bytes\n");
}

static void test_hex_to_bytes_lowercase() {
    uint8_t out[2] = {};
    const size_t n = WmbusPipeline::hex_to_bytes("abcd", out, 2);
    assert(n == 2);
    (void)n;
    assert(out[0] == 0xAB);
    assert(out[1] == 0xCD);
    printf("  PASS: hex_to_bytes lowercase\n");
}

static void test_from_radio_frame() {
    uint8_t sym[24];
    encode_3of6_link_layer(kValidLink12, sizeof(kValidLink12), sym);

    RawRadioFrame raw{};
    std::memcpy(raw.data, sym, sizeof(sym));
    raw.length = sizeof(sym);
    raw.rssi_dbm = -65;
    raw.lqi = 45;
    raw.crc_ok = true;

    auto result = WmbusPipeline::from_radio_frame(raw, 1705312800000LL, 42);
    assert(result.is_ok());

    const auto& frame = result.value();
    assert(frame.raw_hex() == "0B4493157856341234123EBF");
    assert(frame.metadata.rssi_dbm == -65);
    assert(frame.metadata.lqi == 45);
    assert(frame.metadata.crc_ok == true);
    assert(frame.metadata.frame_length == 12);
    assert(frame.metadata.timestamp_ms == 1705312800000LL);
    assert(frame.metadata.rx_count == 42);
    (void)frame.raw_hex().length();
    printf("  PASS: from_radio_frame\n");
}

static void test_from_radio_frame_empty_fails() {
    RawRadioFrame raw{};
    raw.length = 0;
    auto result = WmbusPipeline::from_radio_frame(raw, 0, 0);
    assert(result.is_error());
    assert(result.error() == common::ErrorCode::InvalidArgument);
    printf("  PASS: empty frame rejected\n");
}

static void test_from_radio_frame_bad_crc_fails() {
    uint8_t bad[12];
    std::memcpy(bad, kValidLink12, sizeof(bad));
    bad[10] ^= 0xFF;

    uint8_t sym[24];
    encode_3of6_link_layer(bad, sizeof(bad), sym);

    RawRadioFrame raw{};
    std::memcpy(raw.data, sym, sizeof(sym));
    raw.length = sizeof(sym);
    raw.crc_ok = true;

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 0);
    assert(result.is_error());
    assert(result.error() == common::ErrorCode::FormatInvalid);
    printf("  PASS: bad DLL CRC rejected\n");
}

static void test_from_radio_frame_invalid_3of6_fails() {
    uint8_t sym[24];
    encode_3of6_link_layer(kValidLink12, sizeof(kValidLink12), sym);
    sym[0] = 0xFF;

    RawRadioFrame raw{};
    std::memcpy(raw.data, sym, sizeof(sym));
    raw.length = sizeof(sym);
    raw.crc_ok = true;

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 0);
    assert(result.is_error());
    assert(result.error() == common::ErrorCode::FormatInvalid);
    printf("  PASS: invalid 3-of-6 rejected\n");
}

static void test_frame_l_field() {
    uint8_t sym[24];
    encode_3of6_link_layer(kValidLink12, sizeof(kValidLink12), sym);

    RawRadioFrame raw{};
    std::memcpy(raw.data, sym, sizeof(sym));
    raw.length = sizeof(sym);
    raw.rssi_dbm = -50;
    raw.lqi = 30;
    raw.crc_ok = true;

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_ok());
    assert(result.value().l_field() == 0x0B);
    assert(result.value().c_field() == 0x44);
    printf("  PASS: frame field accessors\n");
}

static void test_identity_and_signature_helpers() {
    uint8_t sym[24];
    encode_3of6_link_layer(kValidLink12, sizeof(kValidLink12), sym);

    RawRadioFrame raw{};
    std::memcpy(raw.data, sym, sizeof(sym));
    raw.length = sizeof(sym);
    raw.crc_ok = true;

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_ok());
    const auto& fr = result.value();
    assert(fr.manufacturer_id() == 0x1593);
    assert(fr.device_id() == 0x78563412);
    assert(fr.identity_key() == "mfg:1593-id:78563412");
    assert(fr.signature_prefix_hex(4) == "0B449315");
    assert(fr.dedup_key().size() == fr.raw_bytes.size());
    (void)fr.l_field();
    printf("  PASS: identity/signature helpers\n");
}

static void test_roundtrip_hex() {
    uint8_t original[] = {0x00, 0x7F, 0x80, 0xFF, 0x01, 0xFE};
    std::string hex = WmbusPipeline::bytes_to_hex(original, 6);
    assert(hex == "007F80FF01FE");

    uint8_t decoded[6] = {};
    const size_t n = WmbusPipeline::hex_to_bytes(hex.c_str(), decoded, 6);
    assert(n == 6);
    (void)n;
    assert(std::memcmp(original, decoded, 6) == 0);
    printf("  PASS: hex roundtrip\n");
}

int main() {
    printf("=== test_wmbus_pipeline ===\n");
    test_bytes_to_hex_basic();
    test_bytes_to_hex_empty();
    test_bytes_to_hex_single();
    test_hex_to_bytes();
    test_hex_to_bytes_lowercase();
    test_from_radio_frame();
    test_from_radio_frame_empty_fails();
    test_from_radio_frame_bad_crc_fails();
    test_from_radio_frame_invalid_3of6_fails();
    test_frame_l_field();
    test_identity_and_signature_helpers();
    test_roundtrip_hex();
    printf("All WMBus pipeline tests passed.\n");
    return 0;
}
