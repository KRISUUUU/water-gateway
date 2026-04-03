#include "wmbus_tmode_rx/wmbus_block_validation.hpp"

namespace wmbus_tmode_rx {

namespace {
constexpr uint16_t kWmbusCrcPolynomial = 0x3D65;
constexpr size_t kFirstBlockDataBytes = 10;
constexpr size_t kFirstBlockTotalBytes = 12;
} // namespace

uint16_t calculate_wmbus_crc16(const uint8_t* data, size_t length) {
    if (!data || length == 0) {
        return 0xFFFF;
    }

    uint16_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8U;
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            if ((crc & 0x8000U) != 0U) {
                crc = static_cast<uint16_t>((crc << 1U) ^ kWmbusCrcPolynomial);
            } else {
                crc <<= 1U;
            }
        }
    }
    return static_cast<uint16_t>(~crc);
}

FirstBlockValidationResult validate_first_block(const uint8_t* decoded, size_t decoded_length) {
    FirstBlockValidationResult result{};
    if (!decoded || decoded_length < kFirstBlockTotalBytes) {
        return result;
    }

    result.expected_crc = calculate_wmbus_crc16(decoded, kFirstBlockDataBytes);
    result.observed_crc = static_cast<uint16_t>(
        (static_cast<uint16_t>(decoded[kFirstBlockDataBytes]) << 8U) |
        static_cast<uint16_t>(decoded[kFirstBlockDataBytes + 1U]));
    result.state = (result.expected_crc == result.observed_crc) ? FirstBlockValidationState::Passed
                                                                : FirstBlockValidationState::Failed;
    return result;
}

} // namespace wmbus_tmode_rx
