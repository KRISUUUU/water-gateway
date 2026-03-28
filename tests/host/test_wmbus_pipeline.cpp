#include "host_test_stubs.hpp"
#include "radio_cc1101/radio_cc1101.hpp"
#include "wmbus_minimal_pipeline/wmbus_frame.hpp"
#include "wmbus_minimal_pipeline/wmbus_pipeline.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace wmbus_minimal_pipeline;
using namespace radio_cc1101;

// Reverse of decode_3of6_symbol (rtl_433 Mode-T table): nibble -> 6-bit codeword 0..63.
static constexpr uint8_t kEnc3of6[16] = {22, 13, 14, 11, 28, 25, 26, 19,
                                         44, 37, 38, 35, 52, 49, 50, 41};

static void encode_3of6_link_layer(const uint8_t* link, size_t link_len, uint8_t* out_symbols) {
    for (size_t i = 0; i < link_len; ++i) {
        out_symbols[i * 2U] = kEnc3of6[(link[i] >> 4) & 0x0FU];
        out_symbols[i * 2U + 1U] = kEnc3of6[link[i] & 0x0FU];
    }
}

static uint16_t crc16_en13757_block(const uint8_t* message, size_t n_bytes) {
    uint16_t remainder = 0;
    for (size_t byte = 0; byte < n_bytes; ++byte) {
        remainder ^= static_cast<uint16_t>(message[byte]) << 8U;
        for (unsigned bit = 0; bit < 8; ++bit) {
            if (remainder & 0x8000U) {
                remainder = static_cast<uint16_t>((remainder << 1) ^ 0x3D65U);
            } else {
                remainder = static_cast<uint16_t>(remainder << 1);
            }
        }
    }
    return static_cast<uint16_t>(~remainder);
}

static std::vector<uint8_t> build_format_a_decoded(const std::vector<uint8_t>& clean_data) {
    std::vector<uint8_t> decoded;
    size_t pos = 0;

    const size_t block1_data = std::min<size_t>(clean_data.size(), 10U);
    decoded.insert(decoded.end(), clean_data.begin(), clean_data.begin() + block1_data);
    const uint16_t block1_crc = crc16_en13757_block(clean_data.data(), block1_data);
    decoded.push_back(static_cast<uint8_t>((block1_crc >> 8U) & 0xFFU));
    decoded.push_back(static_cast<uint8_t>(block1_crc & 0xFFU));
    pos += block1_data;

    while (pos < clean_data.size()) {
        const size_t block_data = std::min<size_t>(clean_data.size() - pos, 16U);
        decoded.insert(decoded.end(), clean_data.begin() + static_cast<std::ptrdiff_t>(pos),
                       clean_data.begin() + static_cast<std::ptrdiff_t>(pos + block_data));
        const uint16_t crc =
            crc16_en13757_block(clean_data.data() + static_cast<std::ptrdiff_t>(pos), block_data);
        decoded.push_back(static_cast<uint8_t>((crc >> 8U) & 0xFFU));
        decoded.push_back(static_cast<uint8_t>(crc & 0xFFU));
        pos += block_data;
    }

    return decoded;
}

static std::vector<uint8_t> encode_3of6_frame(const std::vector<uint8_t>& decoded) {
    std::vector<uint8_t> symbols(decoded.size() * 2U);
    encode_3of6_link_layer(decoded.data(), decoded.size(), symbols.data());
    return symbols;
}

static RawRadioFrame make_raw_frame(const std::vector<uint8_t>& symbols, int8_t rssi_dbm = -65,
                                    uint8_t lqi = 45, bool crc_ok = true) {
    RawRadioFrame raw{};
    std::memcpy(raw.data, symbols.data(), symbols.size());
    raw.length = static_cast<uint16_t>(symbols.size());
    raw.rssi_dbm = rssi_dbm;
    raw.lqi = lqi;
    raw.crc_ok = crc_ok;
    return raw;
}

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
    const std::vector<uint8_t> clean = {0x09, 0x44, 0x93, 0x15, 0x78, 0x56, 0x34, 0x12, 0x01, 0x07};
    const auto decoded = build_format_a_decoded(clean);
    const auto sym = encode_3of6_frame(decoded);
    const RawRadioFrame raw = make_raw_frame(sym);

    auto result = WmbusPipeline::from_radio_frame(raw, 1705312800000LL, 42);
    assert(result.is_ok());

    const auto& frame = result.value();
    assert(frame.raw_hex() == "09449315785634120107");
    assert(frame.metadata.rssi_dbm == -65);
    assert(frame.metadata.lqi == 45);
    assert(frame.metadata.crc_ok == true);
    assert(frame.metadata.frame_length == 10);
    assert(frame.metadata.timestamp_ms == 1705312800000LL);
    assert(frame.metadata.rx_count == 42);
    assert(frame.raw_bytes.size() == 10U);
    assert(frame.l_field() == 0x09);
    assert(frame.manufacturer_id() == 0x1593);
    assert(frame.device_id() == 0x12345678);
    assert(frame.device_version() == 0x01);
    assert(frame.device_type() == 0x07);
    assert(frame.raw_hex().find("D57F") == std::string::npos);
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
    const std::vector<uint8_t> clean = {0x09, 0x44, 0x93, 0x15, 0x78, 0x56, 0x34, 0x12, 0x01, 0x07};
    auto decoded = build_format_a_decoded(clean);
    decoded[10] ^= 0xFFU;

    const auto sym = encode_3of6_frame(decoded);
    const RawRadioFrame raw = make_raw_frame(sym);

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 0);
    assert(result.is_error());
    assert(result.error() == common::ErrorCode::FormatInvalid);
    printf("  PASS: bad DLL CRC rejected\n");
}

static void test_from_radio_frame_invalid_3of6_fails() {
    const std::vector<uint8_t> clean = {0x09, 0x44, 0x93, 0x15, 0x78, 0x56, 0x34, 0x12, 0x01, 0x07};
    const auto decoded = build_format_a_decoded(clean);
    auto sym = encode_3of6_frame(decoded);
    sym[0] = 0xFF;

    const RawRadioFrame raw = make_raw_frame(sym);

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 0);
    assert(result.is_error());
    assert(result.error() == common::ErrorCode::FormatInvalid);
    printf("  PASS: invalid 3-of-6 rejected\n");
}

static void test_frame_l_field() {
    const std::vector<uint8_t> clean = {0x09, 0x44, 0x93, 0x15, 0x78, 0x56, 0x34, 0x12, 0x01, 0x07};
    const auto decoded = build_format_a_decoded(clean);
    const auto sym = encode_3of6_frame(decoded);
    const RawRadioFrame raw = make_raw_frame(sym, -50, 30);

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_ok());
    assert(result.value().l_field() == 0x09);
    assert(result.value().c_field() == 0x44);
    printf("  PASS: frame field accessors\n");
}

static void test_identity_and_signature_helpers() {
    const std::vector<uint8_t> clean = {0x09, 0x44, 0x93, 0x15, 0x78, 0x56, 0x34, 0x12, 0x01, 0x07};
    const auto decoded = build_format_a_decoded(clean);
    const auto sym = encode_3of6_frame(decoded);
    const RawRadioFrame raw = make_raw_frame(sym);

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_ok());
    const auto& fr = result.value();
    assert(fr.manufacturer_id() == 0x1593);
    assert(fr.device_id() == 0x12345678);
    assert(fr.device_version() == 0x01);
    assert(fr.device_type() == 0x07);
    assert(fr.identity_key() == "mfg:1593-id:12345678-t:07");
    assert(fr.signature_prefix_hex(4) == "09449315");
    assert(fr.dedup_key().size() == fr.raw_bytes.size());
    (void)fr.l_field();
    printf("  PASS: identity/signature helpers\n");
}

static void test_from_radio_frame_multiblock_strips_crc() {
    const std::vector<uint8_t> clean = {0x19, 0x44, 0x93, 0x15, 0x78, 0x56, 0x34, 0x12, 0x01,
                                        0x07, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                                        0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
    const auto decoded = build_format_a_decoded(clean);
    assert(decoded.size() == 30U);

    const auto sym = encode_3of6_frame(decoded);
    assert(sym.size() == 60U);

    const RawRadioFrame raw = make_raw_frame(sym);
    auto result = WmbusPipeline::from_radio_frame(raw, 0, 2);
    assert(result.is_ok());

    const auto& frame = result.value();
    assert(frame.raw_bytes == clean);
    assert(frame.raw_bytes.size() == 26U);
    assert(frame.metadata.frame_length == 26U);
    assert(frame.raw_hex().size() == 52U);
    printf("  PASS: multiblock CRC stripping\n");
}

static void test_from_radio_frame_partial_final_block() {
    const std::vector<uint8_t> clean = {0x13, 0x44, 0x93, 0x15, 0x78, 0x56, 0x34, 0x12, 0x01, 0x07,
                                        0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A};
    const auto decoded = build_format_a_decoded(clean);
    assert(decoded.size() == 24U);

    const auto sym = encode_3of6_frame(decoded);
    assert(sym.size() == 48U);

    const RawRadioFrame raw = make_raw_frame(sym);
    auto result = WmbusPipeline::from_radio_frame(raw, 0, 3);
    assert(result.is_ok());

    const auto& frame = result.value();
    assert(frame.raw_bytes == clean);
    assert(frame.raw_bytes.size() == 20U);
    assert(frame.metadata.frame_length == 20U);
    printf("  PASS: partial final block\n");
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
    test_from_radio_frame_multiblock_strips_crc();
    test_from_radio_frame_partial_final_block();
    test_roundtrip_hex();
    printf("All WMBus pipeline tests passed.\n");
    return 0;
}
