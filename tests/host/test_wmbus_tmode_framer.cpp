#include "host_test_stubs.hpp"
#include "wmbus_tmode_rx/wmbus_block_validation.hpp"
#include "wmbus_tmode_rx/wmbus_tmode_framer.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

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
    uint64_t bit_buf = 0;
    int bits_in_buf = 0;

    for (uint8_t byte : bytes) {
        const uint8_t hi = kEncode3of6[(byte >> 4U) & 0x0FU];
        const uint8_t lo = kEncode3of6[byte & 0x0FU];
        bit_buf = (bit_buf << 12U) | (static_cast<uint64_t>(hi) << 6U) | lo;
        bits_in_buf += 12;
        while (bits_in_buf >= 8) {
            bits_in_buf -= 8;
            out.push_back(static_cast<uint8_t>((bit_buf >> bits_in_buf) & 0xFFU));
        }
        if (bits_in_buf > 0) {
            bit_buf &= (1ULL << bits_in_buf) - 1ULL;
        } else {
            bit_buf = 0;
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

std::vector<uint8_t> make_valid_two_block_frame() {
    std::vector<uint8_t> decoded = {
        0x19,
        0x44,
        0x84,
        0x0D,
        0x90,
        0x48,
        0x46,
        0x06,
        0x01,
        0x07,
        0x00,
        0x00,
        0x11,
        0x22,
        0x33,
        0x44,
        0x55,
        0x66,
        0x77,
        0x88,
        0x99,
        0xAA,
        0xBB,
        0xCC,
        0xDD,
        0xEE,
        0xFF,
        0x00,
        0x00,
        0x00,
    };

    const uint16_t first_crc = calculate_wmbus_crc16(decoded.data(), 10);
    decoded[10] = static_cast<uint8_t>((first_crc >> 8U) & 0xFFU);
    decoded[11] = static_cast<uint8_t>(first_crc & 0xFFU);

    const uint16_t second_crc = calculate_wmbus_crc16(decoded.data() + 12U, 16U);
    decoded[28] = static_cast<uint8_t>((second_crc >> 8U) & 0xFFU);
    decoded[29] = static_cast<uint8_t>(second_crc & 0xFFU);
    return decoded;
}

std::vector<uint8_t> make_valid_first_block_frame(uint8_t c_field = 0x44, uint8_t marker = 0x07) {
    std::vector<uint8_t> decoded = {
        0x09,
        c_field,
        0x84,
        0x0D,
        0x90,
        0x48,
        0x46,
        0x06,
        0x01,
        marker,
        0x00,
        0x00,
    };

    const uint16_t crc = calculate_wmbus_crc16(decoded.data(), 10);
    decoded[10] = static_cast<uint8_t>((crc >> 8U) & 0xFFU);
    decoded[11] = static_cast<uint8_t>(crc & 0xFFU);
    return decoded;
}

FeedResult feed_all(WmbusTmodeFramer& framer, const std::vector<uint8_t>& encoded) {
    FeedResult result{};
    for (uint8_t byte : encoded) {
        result = framer.feed_byte(byte);
    }
    return result;
}

void test_valid_normal_orientation() {
    WmbusTmodeFramer framer;
    const auto decoded = make_valid_first_block_frame();
    const auto encoded = encode_3of6(decoded);
    const auto result = feed_all(framer, encoded);

    assert(result.state == FramerState::ExactFrameComplete);
    assert(result.has_complete_frame);
    assert(result.frame.orientation == FrameOrientation::Normal);
    assert(result.frame.decoded_length == decoded.size());
    assert(result.frame.encoded_length == encoded.size());
    assert(result.frame.exact_encoded_bytes_required == encoded.size());
    assert(result.frame.first_block_validation == FirstBlockValidationState::Passed);
    for (size_t i = 0; i < decoded.size(); ++i) {
        assert(result.frame.decoded_bytes[i] == decoded[i]);
    }
    std::printf("  PASS: valid normal orientation\n");
}

void test_valid_reversed_orientation() {
    WmbusTmodeFramer framer;
    const auto decoded = make_valid_first_block_frame(0x46, 0x09);
    const auto encoded = encode_3of6(decoded, true);
    const auto result = feed_all(framer, encoded);

    assert(result.state == FramerState::ExactFrameComplete);
    assert(result.has_complete_frame);
    assert(result.frame.orientation == FrameOrientation::BitReversed);
    for (size_t i = 0; i < decoded.size(); ++i) {
        assert(result.frame.decoded_bytes[i] == decoded[i]);
    }
    std::printf("  PASS: valid reversed orientation\n");
}

void test_invalid_symbol_sequence_rejected() {
    WmbusTmodeFramer framer;
    auto result = framer.feed_byte(0xFF);
    result = framer.feed_byte(0xFF);

    assert(result.state == FramerState::CandidateRejected);
    assert(result.reject_reason == RejectReason::InvalidSymbol);
    std::printf("  PASS: invalid symbol sequence rejected\n");
}

void test_sane_vs_insane_l_field() {
    WmbusTmodeFramer sane_framer;
    const auto sane_decoded = make_valid_first_block_frame();
    const auto sane_encoded = encode_3of6(sane_decoded);
    auto sane_result = sane_framer.feed_byte(sane_encoded.front());
    sane_result = sane_framer.feed_byte(sane_encoded[1]);
    assert(sane_result.state == FramerState::CandidateViable);
    assert(sane_result.normal.l_field_known);
    assert(sane_result.normal.expected_decoded_bytes == sane_decoded.size());

    WmbusTmodeFramer insane_framer;
    auto insane_decoded = sane_decoded;
    insane_decoded[0] = 0x03;
    const auto insane_encoded = encode_3of6(insane_decoded);
    auto insane_result = insane_framer.feed_byte(insane_encoded.front());
    insane_result = insane_framer.feed_byte(insane_encoded[1]);
    assert(insane_result.state == FramerState::CandidateRejected);
    assert(insane_result.reject_reason == RejectReason::LengthOutOfRange);
    std::printf("  PASS: sane vs insane L-field handling\n");
}

void test_exact_encoded_length_calculation() {
    static_assert(WmbusTmodeFramer::encoded_bytes_for_decoded_length(12) == 18,
                  "12 decoded bytes must require 18 encoded bytes");
    static_assert(WmbusTmodeFramer::encoded_bytes_for_decoded_length(11) == 17,
                  "11 decoded bytes must require 17 encoded bytes");
    static_assert(WmbusTmodeFramer::encoded_bytes_for_decoded_length(
                      calculate_format_a_decoded_length(9)) == 18,
                  "format-A L=9 (12 decoded bytes) requires 18 encoded bytes");

    WmbusTmodeFramer framer;
    const auto decoded = make_valid_first_block_frame();
    const auto encoded = encode_3of6(decoded);
    const auto result = feed_all(framer, encoded);

    assert(result.has_complete_frame);
    assert(result.frame.exact_encoded_bytes_required == encoded.size());
    assert(result.normal.exact_encoded_bytes_required == encoded.size());
    std::printf("  PASS: exact encoded-length calculation\n");
}

void test_valid_multi_block_frame_completes() {
    WmbusTmodeFramer framer;
    const auto decoded = make_valid_two_block_frame();
    const auto encoded = encode_3of6(decoded);
    const auto result = feed_all(framer, encoded);

    assert(result.state == FramerState::ExactFrameComplete);
    assert(result.has_complete_frame);
    assert(result.frame.decoded_length == decoded.size());
    assert(result.frame.encoded_length == encoded.size());
    assert(result.frame.exact_encoded_bytes_required == encoded.size());
    std::printf("  PASS: valid multi-block frame completes\n");
}

void test_early_rejection_without_huge_buffer() {
    WmbusTmodeFramer framer;
    auto decoded = make_valid_first_block_frame();
    decoded[10] ^= 0xFFU;
    const auto encoded = encode_3of6(decoded);

    FeedResult result{};
    bool rejected_early = false;
    for (size_t i = 0; i < encoded.size(); ++i) {
        result = framer.feed_byte(encoded[i]);
        if (result.state == FramerState::CandidateRejected) {
            rejected_early = true;
            assert(i + 1U < WmbusTmodeFramer::kMaxEncodedBytes);
            assert(result.reject_reason == RejectReason::FirstBlockValidationFailed);
            break;
        }
    }

    assert(rejected_early);
    std::printf("  PASS: early rejection without huge buffer\n");
}

void test_format_a_length_calculation() {
    assert(calculate_format_a_decoded_length(9) == 12);
    assert(calculate_format_a_decoded_length(25) == 30);
    assert(calculate_format_a_decoded_length(26) == 33);
    std::printf("  PASS: format-A length calculation\n");
}

void test_explicit_ft3_crc() {
    uint8_t raw_data[12] = {0x09, 0x44, 0x84, 0x0D, 0x90, 0x48, 0x46, 0x06, 0x01, 0x07, 0x00, 0x00};
    uint16_t expected_crc = calculate_wmbus_crc16(raw_data, 10);
    raw_data[10] = static_cast<uint8_t>((expected_crc >> 8) & 0xFF);
    raw_data[11] = static_cast<uint8_t>(expected_crc & 0xFF);
    
    auto result = validate_first_block(raw_data, 12);
    assert(result.state == FirstBlockValidationState::Passed);
    
    raw_data[11] ^= 0x01;
    auto result_failed = validate_first_block(raw_data, 12);
    assert(result_failed.state == FirstBlockValidationState::Failed);
    
    std::printf("  PASS: explicit FT3 CRC first-block validation\n");
}

void test_first_block_validation_behavior() {
    const auto decoded = make_valid_first_block_frame();
    auto result = validate_first_block(decoded.data(), 8);
    assert(result.state == FirstBlockValidationState::NotReady);

    result = validate_first_block(decoded.data(), decoded.size());
    assert(result.state == FirstBlockValidationState::Passed);

    auto broken = decoded;
    broken[11] ^= 0x01U;
    result = validate_first_block(broken.data(), broken.size());
    assert(result.state == FirstBlockValidationState::Failed);
    std::printf("  PASS: first-block validation behavior\n");
}

} // namespace

int main() {
    test_valid_normal_orientation();
    test_valid_reversed_orientation();
    test_invalid_symbol_sequence_rejected();
    test_sane_vs_insane_l_field();
    test_exact_encoded_length_calculation();
    test_valid_multi_block_frame_completes();
    test_early_rejection_without_huge_buffer();
    test_explicit_ft3_crc();
    test_first_block_validation_behavior();
    test_format_a_length_calculation();
    return 0;
}
