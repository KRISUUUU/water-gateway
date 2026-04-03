#include "host_test_stubs.hpp"
#include "radio_cc1101/radio_cc1101.hpp"
#include "wmbus_minimal_pipeline/wmbus_frame.hpp"
#include "wmbus_minimal_pipeline/wmbus_pipeline.hpp"
#include "wmbus_tmode_rx/wmbus_block_validation.hpp"
#include "wmbus_tmode_rx/wmbus_tmode_framer.hpp"
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

static RawRadioFrame make_radio_frame_from_payload(const std::vector<uint8_t>& payload,
                                                   bool reverse_bits = false) {
    RawRadioFrame raw{};
    assert(payload.size() <= RawRadioFrame::MAX_DATA_SIZE);
    raw.length = static_cast<uint16_t>(payload.size());
    raw.payload_offset = 0;
    raw.payload_length = static_cast<uint16_t>(payload.size());
    for (size_t i = 0; i < payload.size(); ++i) {
        uint8_t byte = payload[i];
        if (reverse_bits) {
            byte = static_cast<uint8_t>(((byte & 0x01U) << 7U) | ((byte & 0x02U) << 5U) |
                                        ((byte & 0x04U) << 3U) | ((byte & 0x08U) << 1U) |
                                        ((byte & 0x10U) >> 1U) | ((byte & 0x20U) >> 3U) |
                                        ((byte & 0x40U) >> 5U) | ((byte & 0x80U) >> 7U));
        }
        raw.data[i] = byte;
    }
    raw.first_data_byte = payload.empty() ? 0U : payload.front();
    raw.burst_end_reason = RadioBurstEndReason::EmptyPolls;
    return raw;
}

static RawRadioFrame make_radio_frame_from_plain_3of6(const std::vector<uint8_t>& plain,
                                                      bool reverse_bits = false) {
    return make_radio_frame_from_payload(encode_3of6_for_test(plain), reverse_bits);
}

static std::vector<uint8_t> reversed_bits_copy(const std::vector<uint8_t>& bytes) {
    std::vector<uint8_t> out = bytes;
    for (auto& byte : out) {
        byte = static_cast<uint8_t>(((byte & 0x01U) << 7U) | ((byte & 0x02U) << 5U) |
                                    ((byte & 0x04U) << 3U) | ((byte & 0x08U) << 1U) |
                                    ((byte & 0x10U) >> 1U) | ((byte & 0x20U) >> 3U) |
                                    ((byte & 0x40U) >> 5U) | ((byte & 0x80U) >> 7U));
    }
    return out;
}

static std::vector<uint8_t> with_valid_first_block_crc(std::vector<uint8_t> plain) {
    assert(plain.size() >= 12);
    const uint16_t crc = wmbus_tmode_rx::calculate_wmbus_crc16(plain.data(), 10);
    plain[10] = static_cast<uint8_t>((crc >> 8U) & 0xFFU);
    plain[11] = static_cast<uint8_t>(crc & 0xFFU);
    return plain;
}

static wmbus_link::EncodedRxFrame make_exact_frame_from_plain(const std::vector<uint8_t>& plain,
                                                              bool reverse_bits = false) {
    const auto encoded = encode_3of6_for_test(plain);
    wmbus_tmode_rx::WmbusTmodeFramer framer;
    wmbus_tmode_rx::FeedResult feed{};
    for (uint8_t byte : encoded) {
        uint8_t value = byte;
        if (reverse_bits) {
            value = static_cast<uint8_t>(((byte & 0x01U) << 7U) | ((byte & 0x02U) << 5U) |
                                         ((byte & 0x04U) << 3U) | ((byte & 0x08U) << 1U) |
                                         ((byte & 0x10U) >> 1U) | ((byte & 0x20U) >> 3U) |
                                         ((byte & 0x40U) >> 5U) | ((byte & 0x80U) >> 7U));
        }
        feed = framer.feed_byte(value);
    }
    assert(feed.state == wmbus_tmode_rx::FramerState::ExactFrameComplete);
    wmbus_link::EncodedRxFrame frame{};
    frame.encoded_length = feed.frame.encoded_length;
    frame.decoded_length = feed.frame.decoded_length;
    frame.exact_encoded_bytes_required = feed.frame.exact_encoded_bytes_required;
    frame.l_field = feed.frame.l_field;
    frame.orientation = feed.frame.orientation;
    frame.first_block_validation = feed.frame.first_block_validation;
    std::memcpy(frame.encoded_bytes.data(), feed.frame.encoded_bytes.data(), frame.encoded_length);
    std::memcpy(frame.decoded_bytes.data(), feed.frame.decoded_bytes.data(), frame.decoded_length);
    frame.metadata.exact_frame_contract_valid = true;
    frame.metadata.capture_elapsed_ms = 7;
    frame.metadata.captured_frame_length = frame.encoded_length;
    frame.metadata.first_data_byte = frame.encoded_bytes[0];
    frame.metadata.timestamp_ms = 123456;
    frame.metadata.rx_count = 11;
    return frame;
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
    const std::vector<uint8_t> payload = {0x2C, 0x44, 0x93, 0x15, 0x78, 0x56, 0x34, 0x12};
    RawRadioFrame raw = make_radio_frame_from_payload(payload);
    raw.rssi_dbm = -65;
    raw.lqi = 45;
    raw.crc_ok = true;

    auto result = WmbusPipeline::from_radio_frame(raw, 1705312800000LL, 42);
    assert(result.is_ok());

    auto& frame = result.value();
    assert(frame.canonical_hex() == "2C44931578563412");
    assert(frame.captured_hex() == "2C44931578563412");
    assert(frame.decoded_ok == false);
    assert(frame.metadata.rssi_dbm == -65);
    assert(frame.metadata.lqi == 45);
    assert(frame.metadata.crc_ok == true);
    assert(frame.metadata.radio_crc_available == false);
    assert(frame.metadata.raw_frame_contract_valid == true);
    assert(frame.metadata.burst_end_reason == RadioBurstEndReason::EmptyPolls);
    assert(frame.metadata.first_data_byte == 0x2C);
    assert(frame.metadata.payload_offset == 0);
    assert(frame.metadata.payload_length == 8);
    assert(frame.metadata.captured_frame_length == 8);
    assert(frame.metadata.canonical_frame_length == 8);
    assert(frame.metadata.timestamp_ms == 1705312800000LL);
    assert(frame.metadata.rx_count == 42);
    printf("  PASS: from_radio_frame\n");
}

static void test_from_exact_frame() {
    const std::vector<uint8_t> plain = with_valid_first_block_crc(
        {0x0B, 0x44, 0x84, 0x0D, 0x90, 0x48, 0x46, 0x06, 0x01, 0x07, 0x00, 0x00});
    auto exact = make_exact_frame_from_plain(plain);

    auto result = WmbusPipeline::from_exact_frame(exact);
    assert(result.is_ok());
    const auto& frame = result.value();
    assert(frame.decoded_ok);
    assert(frame.raw_bytes == plain);
    assert(frame.original_raw_bytes.size() == exact.encoded_length);
    assert(frame.metadata.raw_frame_contract_valid);
    assert(frame.metadata.timestamp_ms == 123456);
    assert(frame.metadata.rx_count == 11);
    printf("  PASS: from_exact_frame\n");
}

static void test_from_exact_frame_rejects_invalid_link_payload() {
    const std::vector<uint8_t> plain = with_valid_first_block_crc(
        {0x0B, 0x44, 0x84, 0x0D, 0x90, 0x48, 0x46, 0x06, 0x01, 0x07, 0x00, 0x00});
    auto exact = make_exact_frame_from_plain(plain);
    exact.decoded_bytes[11] ^= 0x01U;

    auto result = WmbusPipeline::from_exact_frame(exact);
    assert(result.is_error());
    assert(result.error() == common::ErrorCode::ValidationFailed);
    printf("  PASS: invalid exact frame rejected\n");
}

static void test_from_radio_frame_empty_fails() {
    RawRadioFrame raw{};
    raw.length = 0;
    auto result = WmbusPipeline::from_radio_frame(raw, 0, 0);
    assert(result.is_error());
    assert(result.error() == common::ErrorCode::InvalidArgument);
    printf("  PASS: empty frame rejected\n");
}

static void test_invalid_raw_radio_contract_rejected() {
    RawRadioFrame raw{};
    raw.data[0] = 0x2C;
    raw.data[1] = 0x44;
    raw.length = 3;
    raw.payload_offset = 0;
    raw.payload_length = 2;
    raw.first_data_byte = raw.data[0];

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_error());
    assert(result.error() == common::ErrorCode::InvalidArgument);
    printf("  PASS: invalid RawRadioFrame contract rejected\n");
}

static void test_identity_and_signature_helpers() {
    const std::vector<uint8_t> payload = {0x2C, 0x44, 0x93, 0x15, 0x78,
                                          0x56, 0x34, 0x12, 0xAA, 0xBB};
    RawRadioFrame raw = make_radio_frame_from_payload(payload);
    raw.crc_ok = true;

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_ok());
    const auto& frame = result.value();
    assert(frame.l_field() == 0);
    assert(frame.c_field() == 0);
    assert(frame.manufacturer_id() == 0x0000);
    assert(frame.device_id() == 0x00000000);
    assert(!frame.has_reliable_identity());
    assert(frame.identity_key() == "sig:2C44931578563412AABB");
    assert(frame.signature_prefix_hex(4) == "2C449315");
    assert(frame.dedup_key().size() == frame.raw_bytes.size());
    printf("  PASS: identity/signature helpers\n");
}

static void test_identity_key_falls_back_to_signature_for_zero_fields() {
    const std::vector<uint8_t> plain = with_valid_first_block_crc(
        {0x0B, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x07, 0x00, 0x00});
    RawRadioFrame raw = make_radio_frame_from_plain_3of6(plain);

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_ok());
    const auto& frame = result.value();
    assert(frame.decoded_ok == true);
    assert(frame.l_field() == 0x0B);
    assert(frame.manufacturer_id() == 0x0000);
    assert(frame.device_id() == 0x00000000);
    assert(frame.identity_key().rfind("sig:", 0) == 0);
    printf("  PASS: zero manufacturer/device falls back to signature\n");
}

static void test_decode_3of6_sample_frame() {
    RawRadioFrame raw{};
    raw.length = static_cast<uint16_t>(
        WmbusPipeline::hex_to_bytes("0F6356F86798C8811BCC609DB01997D716", raw.data, sizeof(raw.data)));
    raw.payload_offset = 0;
    raw.payload_length = raw.length;
    raw.first_data_byte = raw.data[0];

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_ok());
    assert(result.value().decoded_ok == false);
    printf("  PASS: undecodable captured sample kept as raw burst\n");
}

static void test_decode_3of6_invalid_symbol_falls_back_to_raw() {
    RawRadioFrame raw = make_radio_frame_from_payload({0x00, 0x00});

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_ok());
    const auto& frame = result.value();
    assert(frame.decoded_ok == false);
    assert(frame.canonical_hex() == "0000");
    assert(frame.captured_hex() == "0000");
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
    const std::vector<uint8_t> plain = with_valid_first_block_crc(
        {0x0B, 0x44, 0x84, 0x0D, 0x90, 0x48, 0x46, 0x06, 0x01, 0x07, 0x00, 0x00});
    RawRadioFrame raw = make_radio_frame_from_plain_3of6(plain);

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_ok());
    const auto& frame = result.value();
    assert(frame.decoded_ok == true);
    assert(frame.raw_bytes == plain);
    assert(frame.l_field() == 0x0B);
    assert(frame.c_field() == 0x44);
    assert(frame.canonical_hex() == WmbusPipeline::bytes_to_hex(plain));
    assert(frame.captured_hex() != frame.canonical_hex());
    assert(frame.metadata.raw_frame_contract_valid == true);
    assert(frame.metadata.first_data_byte == raw.data[0]);
    assert(frame.metadata.payload_offset == 0);
    assert(frame.metadata.payload_length == raw.payload_length);
    assert(frame.metadata.captured_frame_length == raw.length);
    assert(frame.metadata.canonical_frame_length == plain.size());
    assert(frame.identity_key() == "mfg:0D84-id:06464890");
    printf("  PASS: known 3-of-6 payload decoded\n");
}

static void test_decode_3of6_diehl_dme_payload() {
    const std::vector<uint8_t> plain = with_valid_first_block_crc(
        {0x0B, 0x44, 0x84, 0x0D, 0x90, 0x48, 0x46, 0x06, 0x01, 0x07, 0x00, 0x00});
    RawRadioFrame raw = make_radio_frame_from_plain_3of6(plain);

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

static void test_decode_3of6_reversed_bit_order_supported() {
    const std::vector<uint8_t> plain = with_valid_first_block_crc(
        {0x0B, 0x44, 0x84, 0x0D, 0x90, 0x48, 0x46, 0x06, 0x01, 0x07, 0x00, 0x00});
    RawRadioFrame raw = make_radio_frame_from_plain_3of6(plain, true);

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_ok());
    const auto& frame = result.value();
    assert(frame.decoded_ok == true);
    assert(frame.raw_bytes == plain);
    printf("  PASS: reversed-bit 3-of-6 payload decoded\n");
}

static void test_decode_3of6_accepts_trailing_invalid_noise() {
    const std::vector<uint8_t> plain = with_valid_first_block_crc(
        {0x0B, 0x44, 0x84, 0x0D, 0x90, 0x48, 0x46, 0x06, 0x01, 0x07, 0x00, 0x00});
    std::vector<uint8_t> encoded = encode_3of6_for_test(plain);
    encoded.push_back(0xFF);
    encoded.push_back(0xFF);
    RawRadioFrame raw = make_radio_frame_from_payload(encoded);

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_ok());
    const auto& frame = result.value();
    assert(frame.decoded_ok == false);
    assert(frame.captured_hex() == WmbusPipeline::bytes_to_hex(encoded));
    printf("  PASS: trailing invalid noise is rejected by exact-frame contract\n");
}

static void test_decode_3of6_trims_extra_decodable_bytes_by_l_field() {
    const std::vector<uint8_t> plain = with_valid_first_block_crc(
        {0x0B, 0x44, 0x84, 0x0D, 0x90, 0x48, 0x46, 0x06, 0x01, 0x07, 0x00, 0x00});
    std::vector<uint8_t> extended = plain;
    extended.push_back(0xAB);
    extended.push_back(0xCD);
    RawRadioFrame raw = make_radio_frame_from_plain_3of6(extended);

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_ok());
    const auto& frame = result.value();
    assert(frame.decoded_ok == false);
    assert(frame.metadata.captured_frame_length == raw.length);
    printf("  PASS: extra decodable suffix is rejected by exact-frame contract\n");
}

static void test_reverse_bits_retry_runs_after_partial_invalid_forward_decode() {
    const std::vector<uint8_t> plain = with_valid_first_block_crc(
        {0x0B, 0x44, 0x84, 0x0D, 0x90, 0x48, 0x46, 0x06, 0x01, 0x07, 0x00, 0x00});
    std::vector<uint8_t> reversed = reversed_bits_copy(encode_3of6_for_test(plain));
    reversed.push_back(0xFF);
    reversed.push_back(0xFF);
    RawRadioFrame raw = make_radio_frame_from_payload(reversed);

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_ok());
    const auto& frame = result.value();
    assert(frame.decoded_ok == false);
    printf("  PASS: reverse-bit candidate with trailing noise is rejected by exact-frame contract\n");
}

static void test_decode_3of6_does_not_accept_wrong_offset() {
    const std::vector<uint8_t> plain = {0x0B, 0x44, 0x84, 0x0D, 0x90, 0x48,
                                        0x46, 0x06, 0x01, 0x07, 0x00, 0x00};
    std::vector<uint8_t> encoded = encode_3of6_for_test(plain);
    encoded.insert(encoded.begin(), 0x00);
    RawRadioFrame raw = make_radio_frame_from_payload(encoded);

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_ok());
    const auto& frame = result.value();
    assert(frame.decoded_ok == false);
    assert(frame.identity_key().rfind("sig:", 0) == 0);
    printf("  PASS: wrong bit offset is rejected\n");
}

static void test_decode_3of6_rejects_structurally_invalid_candidate() {
    const std::vector<uint8_t> plain = with_valid_first_block_crc(
        {0xAA, 0x44, 0x84, 0x0D, 0x90, 0x48, 0x46, 0x06, 0x01, 0x07, 0x00, 0x00});
    RawRadioFrame raw = make_radio_frame_from_plain_3of6(plain);

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_ok());
    const auto& frame = result.value();
    assert(frame.decoded_ok == false);
    assert(frame.manufacturer_id() == 0x0000);
    assert(frame.device_id() == 0x00000000);
    assert(frame.identity_key().rfind("sig:", 0) == 0);
    printf("  PASS: structurally invalid decoded candidate rejected\n");
}

static void test_decode_3of6_rejects_truncated_payload() {
    const std::vector<uint8_t> plain = {0x0B, 0x44, 0x84, 0x0D, 0x90, 0x48,
                                        0x46, 0x06, 0x01, 0x07, 0x00, 0x00};
    std::vector<uint8_t> encoded = encode_3of6_for_test(plain);
    assert(!encoded.empty());
    encoded.pop_back();
    RawRadioFrame raw = make_radio_frame_from_payload(encoded);

    auto result = WmbusPipeline::from_radio_frame(raw, 0, 1);
    assert(result.is_ok());
    const auto& frame = result.value();
    assert(frame.decoded_ok == false);
    printf("  PASS: truncated encoded payload rejected\n");
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
    test_from_exact_frame();
    test_from_exact_frame_rejects_invalid_link_payload();
    test_from_radio_frame_empty_fails();
    test_invalid_raw_radio_contract_rejected();
    test_identity_and_signature_helpers();
    test_identity_key_falls_back_to_signature_for_zero_fields();
    test_decode_3of6_sample_frame();
    test_decode_3of6_invalid_symbol_falls_back_to_raw();
    test_decode_3of6_empty_frame_rejected();
    test_decode_3of6_known_encoded_payload();
    test_decode_3of6_diehl_dme_payload();
    test_decode_3of6_reversed_bit_order_supported();
    test_decode_3of6_accepts_trailing_invalid_noise();
    test_decode_3of6_trims_extra_decodable_bytes_by_l_field();
    test_reverse_bits_retry_runs_after_partial_invalid_forward_decode();
    test_decode_3of6_does_not_accept_wrong_offset();
    test_decode_3of6_rejects_structurally_invalid_candidate();
    test_decode_3of6_rejects_truncated_payload();
    test_roundtrip_hex();
    printf("All WMBus pipeline tests passed.\n");
    return 0;
}
