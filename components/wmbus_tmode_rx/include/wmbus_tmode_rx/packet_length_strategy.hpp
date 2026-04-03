#pragma once

#include "wmbus_tmode_rx/wmbus_tmode_framer.hpp"

#include <cstdint>

namespace wmbus_tmode_rx {

enum class HybridPacketMode : uint8_t {
    Infinite = 0,
    FixedLengthTail,
};

enum class PacketLengthDecisionReason : uint8_t {
    LengthUnknown = 0,
    AlreadyFixedLength,
    NoRemainingBytes,
    RemainingExceedsHardwareLimit,
    SafeToSwitch,
};

struct PacketLengthTransitionDecision {
    HybridPacketMode current_mode = HybridPacketMode::Infinite;
    HybridPacketMode desired_mode = HybridPacketMode::Infinite;
    PacketLengthDecisionReason reason = PacketLengthDecisionReason::LengthUnknown;
    bool should_switch = false;
    uint16_t exact_encoded_length = 0;
    uint16_t remaining_encoded_length = 0;
    uint8_t fixed_length_register_value = 0;
};

PacketLengthTransitionDecision evaluate_packet_length_transition(
    const CandidateProgress& progress, HybridPacketMode current_mode);

uint16_t exact_read_window_bytes(const CandidateProgress& progress, uint16_t fifo_bytes_available);

} // namespace wmbus_tmode_rx
