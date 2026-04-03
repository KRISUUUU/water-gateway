#pragma once

#include <cstddef>
#include <cstdint>

namespace wmbus_tmode_rx {

enum class FirstBlockValidationState : uint8_t {
    NotReady = 0,
    Passed,
    Failed,
};

struct FirstBlockValidationResult {
    FirstBlockValidationState state = FirstBlockValidationState::NotReady;
    uint16_t expected_crc = 0;
    uint16_t observed_crc = 0;
};

// Wireless M-Bus link-layer first-block validation helper.
// The refactor's earliest safe structural validation point is the first 10 data bytes and their
// following 2-byte block CRC. This helper stays pure and host-testable.
FirstBlockValidationResult validate_first_block(const uint8_t* decoded, size_t decoded_length);

uint16_t calculate_wmbus_crc16(const uint8_t* data, size_t length);

} // namespace wmbus_tmode_rx
