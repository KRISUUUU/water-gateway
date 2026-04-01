#include "host_test_stubs.hpp"
#include "radio_cc1101/radio_cc1101.hpp"
#include "wmbus_minimal_pipeline/wmbus_frame.hpp"
#include "wmbus_minimal_pipeline/wmbus_pipeline.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace wmbus_minimal_pipeline;
using namespace radio_cc1101;

static const uint8_t kEncode3of6[16] = {0x16, 0x0D, 0x0E, 0x0B, 0x1C, 0x19, 0x1A, 0x13,
                                        0x2C, 0x25, 0x26, 0x23, 0x34, 0x31, 0x32, 0x29};

static std::vector<uint8_t> encode_3of6_for_test(const std::vector<uint8_t>& bytes) {
    std::vector<uint8_t> out;
    uint32_t bit_buf = 0;
    int bits_in_buf = 0;

    for (uint8_t byte : bytes) {
        const uint8_t hi = kEncode3of6[(byte >> 4) & 0x0F];
        const uint8_t lo = kEncode3of6[byte & 0x0F];
        bit_buf = (bit_buf << 12) | (static_cast<uint32_t>(hi) << 6) | lo;
        bits_in_buf += 12;
        while (bits_in_buf >= 8) {
            bits_in_buf -= 8;
            out.push_back(static_cast<uint8_t>((bit_buf >> bits_in_buf) & 0xFF));
        }
    }

    if (bits_in_buf > 0) {
        out.push_back(static_cast<uint8_t>((bit_buf << (8 - bits_in_buf)) & 0xFF));
    }

    return out;
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
    size_t n = WmbusPipeline::hex_to_bytes("2C449315", out, 4);
    assert(n == 4);
    assert(out[0] == 0x2C);
    assert(out[1] == 0x44);
    assert(out[2] == 0x93);
    assert(out[3] == 0x15);
    printf("  PASS: hex_to_bytes\n");
}

static void test_hex_to_bytes_lowercase() {
    uint8_t out[2] = {};
    size_t n = WmbusPipeline::hex_to_bytes("abcd", out, 2);
    assert(n == 2);
    assert(out[0] == 0xAB);
    assert(out[1] == 0xCD);
    printf("  PASS: hex_to_bytes lowercase\n");
}

static void test_from_radio_frame() {
    RawRadioFrame raw{};
    raw.data[0] = 0x2C;
    raw.data[1] = 0x44;
    raw.data[2] = 0x93;
    raw.data[3] = 0x15;
    raw.data[4] = 0x78;
    raw.data[5] = 0x56;
    raw.data[6] = 0x34;
    raw.data[7] = 0x12;
    raw.length = 8;
    raw.rssi_dbm = -65;
    raw.lqi = 45;
    raw.crc_ok = true;

    auto result = WmbusPipeline::from_radio_frame(raw, 1705312800000LL, 42);
    assert(result.is_ok());

    auto& frame = result.value();
    assert(frame.raw_hex() == "2C44931578563412");
    assert(frame.original_raw_hex() == "2C44931578563412");
    assert(frame.decoded_ok == false);
    assert(frame.metadata.rssi_dbm == -65);
    assert(frame.metadata.lqi == 45);
    assert(frame.metadata.crc_ok == true);
    assert(frame.metadata.frame_length == 8);
    assert(frame.metadata.timestamp_ms == 1705312800000LL);
    assert(frame.metadata.rx_count == 42);
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

static void test_frame_l_field() {
    RawRadioFrame raw{};
    raw.data[0] = 0x2C; // L-field = 44
    raw.data[1] = 0x44; // C-field
    raw.length = 2;
    raw.rssi_dbm = -50;
    raw.lqi = 30;
    raw.crc_ok = true;

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_ok());
    assert(result.value().l_field() == 0x2C);
    assert(result.value().c_field() == 0x44);
    printf("  PASS: frame field accessors\n");
}

static void test_identity_and_signature_helpers() {
    RawRadioFrame raw{};
    raw.data[0] = 0x2C;
    raw.data[1] = 0x44;
    raw.data[2] = 0x93;
    raw.data[3] = 0x15;
    raw.data[4] = 0x78;
    raw.data[5] = 0x56;
    raw.data[6] = 0x34;
    raw.data[7] = 0x12;
    raw.data[8] = 0xAA;
    raw.data[9] = 0xBB;
    raw.length = 10;
    raw.crc_ok = true;

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_ok());
    const auto& frame = result.value();
    assert(frame.manufacturer_id() == 0x1593);
    assert(frame.device_id() == 0x12345678);
    assert(frame.identity_key() == "sig:2C44931578563412AABB");
    assert(frame.signature_prefix_hex(4) == "2C449315");
    assert(frame.dedup_key().size() == frame.raw_bytes.size());
    printf("  PASS: identity/signature helpers\n");
}

static void test_identity_key_falls_back_to_signature_for_zero_fields() {
    RawRadioFrame raw{};
    raw.data[0] = 0x2C;
    raw.data[1] = 0x44;
    raw.data[2] = 0x00;
    raw.data[3] = 0x00;
    raw.data[4] = 0x00;
    raw.data[5] = 0x00;
    raw.data[6] = 0x00;
    raw.data[7] = 0x00;
    raw.data[8] = 0xAA;
    raw.data[9] = 0xBB;
    raw.length = 10;

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_ok());
    const auto& frame = result.value();
    assert(frame.manufacturer_id() == 0x0000);
    assert(frame.device_id() == 0x00000000);
    assert(frame.identity_key() == "sig:2C44000000000000AABB");
    printf("  PASS: zero manufacturer/device falls back to signature\n");
}

static void test_decode_3of6_sample_frame() {
    RawRadioFrame raw{};
    raw.length = static_cast<uint16_t>(WmbusPipeline::hex_to_bytes(
        "0F6356F86798C8811BCC609DB01997D716", raw.data, sizeof(raw.data)));

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_ok());
    const auto& frame = result.value();
    assert(frame.decoded_ok == false);
    assert(frame.original_raw_hex() == "0F6356F86798C8811BCC609DB01997D716");
    assert(frame.raw_hex() == "0F6356F86798C8811BCC609DB01997D716");
    assert(frame.metadata.frame_length == frame.raw_bytes.size());
    printf("  PASS: undecodable sample frame falls back to raw\n");
}

static void test_decode_3of6_invalid_symbol_falls_back_to_raw() {
    RawRadioFrame raw{};
    raw.data[0] = 0x00;
    raw.data[1] = 0x00;
    raw.length = 2;

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_ok());
    const auto& frame = result.value();
    assert(frame.decoded_ok == false);
    assert(frame.raw_hex() == "0000");
    assert(frame.original_raw_hex() == "0000");
    printf("  PASS: invalid 3-of-6 symbols fall back to raw\n");
}

static void test_decode_3of6_empty_frame_rejected() {
    RawRadioFrame raw{};
    raw.length = 0;
    auto result = WmbusPipeline::from_radio_frame(raw, 0, 0);
    assert(result.is_error());
    printf("  PASS: empty 3-of-6 frame rejected\n");
}

static void test_decode_3of6_known_encoded_payload() {
    const std::vector<uint8_t> plain = {0x2C, 0x44, 0x93, 0x15, 0x78, 0x56, 0x34, 0x12};
    const std::vector<uint8_t> encoded = encode_3of6_for_test(plain);

    RawRadioFrame raw{};
    assert(encoded.size() <= sizeof(raw.data));
    std::memcpy(raw.data, encoded.data(), encoded.size());
    raw.length = static_cast<uint16_t>(encoded.size());

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_ok());
    const auto& frame = result.value();
    assert(frame.decoded_ok == true);
    assert(frame.raw_bytes == plain);
    assert(frame.raw_hex() == "2C44931578563412");
    assert(frame.identity_key() == "mfg:1593-id:12345678");
    printf("  PASS: known 3-of-6 payload decoded\n");
}

static void test_decode_3of6_diehl_dme_payload() {
    const std::vector<uint8_t> plain = {0x0B, 0x44, 0x84, 0x0D, 0x90, 0x48,
                                        0x46, 0x06, 0x01, 0x07, 0x00, 0x00};
    const std::vector<uint8_t> encoded = encode_3of6_for_test(plain);

    RawRadioFrame raw{};
    assert(encoded.size() <= sizeof(raw.data));
    std::memcpy(raw.data, encoded.data(), encoded.size());
    raw.length = static_cast<uint16_t>(encoded.size());

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_ok());
    const auto& frame = result.value();
    assert(frame.decoded_ok == true);
    assert(frame.raw_bytes == plain);
    assert(frame.manufacturer_id() == 0x0D84);
    assert(frame.device_id() == 0x06464890);
    assert(frame.identity_key() == "mfg:0D84-id:06464890");
    printf("  PASS: Diehl DME 3-of-6 payload decoded\n");
}

static void test_roundtrip_hex() {
    uint8_t original[] = {0x00, 0x7F, 0x80, 0xFF, 0x01, 0xFE};
    std::string hex = WmbusPipeline::bytes_to_hex(original, 6);
    assert(hex == "007F80FF01FE");

    uint8_t decoded[6] = {};
    size_t n = WmbusPipeline::hex_to_bytes(hex.c_str(), decoded, 6);
    assert(n == 6);
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
    test_frame_l_field();
    test_identity_and_signature_helpers();
    test_identity_key_falls_back_to_signature_for_zero_fields();
    test_decode_3of6_sample_frame();
    test_decode_3of6_invalid_symbol_falls_back_to_raw();
    test_decode_3of6_empty_frame_rejected();
    test_decode_3of6_known_encoded_payload();
    test_decode_3of6_diehl_dme_payload();
    test_roundtrip_hex();
    printf("All WMBus pipeline tests passed.\n");
    return 0;
}
