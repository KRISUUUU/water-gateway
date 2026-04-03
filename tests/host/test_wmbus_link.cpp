#include "host_test_stubs.hpp"
#include "wmbus_link/wmbus_link.hpp"
#include "wmbus_tmode_rx/wmbus_block_validation.hpp"
#include "wmbus_tmode_rx/wmbus_tmode_framer.hpp"

#include <cassert>
#include <cstdio>
#include <vector>

using namespace wmbus_link;
using namespace wmbus_tmode_rx;

namespace {

constexpr uint8_t kEncode3of6[16] = {0x16, 0x0D, 0x0E, 0x0B, 0x1C, 0x19, 0x1A, 0x13,
                                     0x2C, 0x25, 0x26, 0x23, 0x34, 0x31, 0x32, 0x29};

uint8_t reverse_bits8(uint8_t value) {
    return static_cast<uint8_t>(((value & 0x01U) << 7U) | ((value & 0x02U) << 5U) |
                                ((value & 0x04U) << 3U) | ((value & 0x08U) << 1U) |
                                ((value & 0x10U) >> 1U) | ((value & 0x20U) >> 3U) |
                                ((value & 0x40U) >> 5U) | ((value & 0x80U) >> 7U));
}

std::vector<uint8_t> encode_3of6(const std::vector<uint8_t>& bytes, bool reverse_encoded = false) {
    std::vector<uint8_t> out;
    uint32_t bit_buf = 0;
    int bits_in_buf = 0;

    for (uint8_t byte : bytes) {
        const uint8_t hi = kEncode3of6[(byte >> 4U) & 0x0FU];
        const uint8_t lo = kEncode3of6[byte & 0x0FU];
        bit_buf = (bit_buf << 12U) | (static_cast<uint32_t>(hi) << 6U) | lo;
        bits_in_buf += 12;
        while (bits_in_buf >= 8) {
            bits_in_buf -= 8;
            out.push_back(static_cast<uint8_t>((bit_buf >> bits_in_buf) & 0xFFU));
        }
    }
    if (bits_in_buf > 0) {
        out.push_back(static_cast<uint8_t>((bit_buf << (8 - bits_in_buf)) & 0xFFU));
    }
    if (reverse_encoded) {
        for (auto& byte : out) {
            byte = reverse_bits8(byte);
        }
    }
    return out;
}

std::vector<uint8_t> make_valid_frame(uint8_t c_field = 0x44, uint8_t marker = 0x07) {
    std::vector<uint8_t> decoded = {
        0x09, c_field, 0x84, 0x0D, 0x90, 0x48, 0x46, 0x06, 0x01, marker, 0x00, 0x00,
    };
    const uint16_t crc = calculate_wmbus_crc16(decoded.data(), 10);
    decoded[10] = static_cast<uint8_t>((crc >> 8U) & 0xFFU);
    decoded[11] = static_cast<uint8_t>(crc & 0xFFU);
    return decoded;
}

EncodedRxFrame exact_frame_from_encoded(const std::vector<uint8_t>& encoded, int64_t timestamp_ms = 1234,
                                        uint32_t rx_count = 9) {
    WmbusTmodeFramer framer;
    FeedResult result{};
    for (uint8_t byte : encoded) {
        result = framer.feed_byte(byte);
    }
    assert(result.has_complete_frame);
    EncodedRxFrame frame{};
    frame.encoded_length = result.frame.encoded_length;
    frame.decoded_length = result.frame.decoded_length;
    frame.exact_encoded_bytes_required = result.frame.exact_encoded_bytes_required;
    frame.l_field = result.frame.l_field;
    frame.orientation = result.frame.orientation;
    frame.first_block_validation = result.frame.first_block_validation;
    for (uint16_t i = 0; i < frame.encoded_length; ++i) {
        frame.encoded_bytes[i] = result.frame.encoded_bytes[i];
    }
    for (uint16_t i = 0; i < frame.decoded_length; ++i) {
        frame.decoded_bytes[i] = result.frame.decoded_bytes[i];
    }
    frame.metadata.exact_frame_contract_valid = true;
    frame.metadata.timestamp_ms = timestamp_ms;
    frame.metadata.rx_count = rx_count;
    return frame;
}

void test_valid_tmode_frame_end_to_end() {
    const auto encoded = encode_3of6(make_valid_frame());
    const auto frame = exact_frame_from_encoded(encoded);
    const auto exact = WmbusLink::validate_exact_frame(frame);
    const auto link = WmbusLink::validate_and_build(frame);

    assert(exact.accepted);
    assert(link.accepted);
    assert(link.telegram.link.metadata.canonical_length == 12);
    assert(link.telegram.identity_key() == "mfg:0D84-id:06464890");
    assert(link.telegram.captured_hex() == WmbusLink::bytes_to_hex(frame.encoded_bytes.data(), frame.encoded_length));
    std::printf("  PASS: valid T-mode frame end-to-end\n");
}

void test_invalid_frame_rejected_cleanly() {
    auto encoded = encode_3of6(make_valid_frame());
    auto frame = exact_frame_from_encoded(encoded);
    frame.decoded_bytes[10] ^= 0xFFU;
    frame.first_block_validation = FirstBlockValidationState::Failed;

    const auto exact = WmbusLink::validate_exact_frame(frame);
    const auto link = WmbusLink::validate_and_build(frame);
    assert(!exact.accepted);
    assert(exact.reject_reason == ExactFrameRejectReason::InvalidFirstBlock);
    assert(!link.accepted);
    std::printf("  PASS: invalid frame rejected cleanly\n");
}

void test_correct_identity_extraction() {
    const auto encoded = encode_3of6(make_valid_frame());
    const auto link = WmbusLink::validate_and_build(exact_frame_from_encoded(encoded));

    assert(link.accepted);
    assert(link.telegram.manufacturer_id() == 0x0D84);
    assert(link.telegram.device_id() == 0x06464890);
    assert(link.telegram.has_reliable_identity());
    std::printf("  PASS: correct identity extraction\n");
}

void test_correct_canonical_bytes() {
    const auto decoded = make_valid_frame();
    const auto link = WmbusLink::validate_and_build(exact_frame_from_encoded(encode_3of6(decoded)));

    assert(link.accepted);
    for (size_t i = 0; i < decoded.size(); ++i) {
        assert(link.telegram.link.canonical_bytes[i] == decoded[i]);
    }
    std::printf("  PASS: correct canonical bytes\n");
}

void test_reverse_orientation_valid_case() {
    const auto decoded = make_valid_frame(0x46, 0x09);
    const auto link = WmbusLink::validate_and_build(exact_frame_from_encoded(encode_3of6(decoded, true)));

    assert(link.accepted);
    assert(link.telegram.link.metadata.orientation == FrameOrientation::BitReversed);
    assert(link.telegram.identity_key() == "mfg:0D84-id:06464890");
    std::printf("  PASS: reverse-orientation valid case\n");
}

void test_exact_frame_contract_is_unambiguous() {
    const auto encoded = encode_3of6(make_valid_frame());
    auto frame = exact_frame_from_encoded(encoded);
    frame.encoded_length += 1U;
    const auto exact = WmbusLink::validate_exact_frame(frame);
    assert(!exact.accepted);
    assert(exact.reject_reason == ExactFrameRejectReason::InvalidLength);
    std::printf("  PASS: exact-frame contract is unambiguous\n");
}

void test_continuation_block_crc() {
    std::vector<uint8_t> decoded = {
        0x19, // L-field = 25. 1 + 25 + 4 = 30 bytes
        0x44, 0x84, 0x0D, 0x90, 0x48, 0x46, 0x06, 0x01, 0x07, // 9 user
        0x00, 0x00, // CRC 1
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, // 16 user
        0x00, 0x00 // CRC 2
    };
    uint16_t first_crc = calculate_wmbus_crc16(decoded.data(), 10);
    decoded[10] = static_cast<uint8_t>((first_crc >> 8) & 0xFF);
    decoded[11] = static_cast<uint8_t>(first_crc & 0xFF);
    
    uint16_t second_crc = calculate_wmbus_crc16(decoded.data() + 12, 16);
    decoded[28] = static_cast<uint8_t>((second_crc >> 8) & 0xFF);
    decoded[29] = static_cast<uint8_t>(second_crc & 0xFF);

    auto encoded = encode_3of6(decoded);
    auto frame = exact_frame_from_encoded(encoded);
    auto link = WmbusLink::validate_and_build(frame);
    assert(link.accepted);

    frame.decoded_bytes[29] ^= 0x01;
    auto link_failed = WmbusLink::validate_and_build(frame);
    assert(!link_failed.accepted);
    assert(link_failed.reject_reason == LinkRejectReason::BlockValidationFailed);
    
    std::printf("  PASS: continuation block CRC validation\n");
}

} // namespace

int main() {
    test_valid_tmode_frame_end_to_end();
    test_invalid_frame_rejected_cleanly();
    test_correct_identity_extraction();
    test_correct_canonical_bytes();
    test_reverse_orientation_valid_case();
    test_exact_frame_contract_is_unambiguous();
    test_continuation_block_crc();
    return 0;
}
