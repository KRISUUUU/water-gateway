#include "wmbus_tmode_rx/packet_length_strategy.hpp"

#include <algorithm>

namespace wmbus_tmode_rx {

PacketLengthTransitionDecision evaluate_packet_length_transition(
    const CandidateProgress& progress, HybridPacketMode current_mode) {
    PacketLengthTransitionDecision decision{};
    decision.current_mode = current_mode;
    decision.desired_mode = current_mode;
    decision.exact_encoded_length = progress.exact_encoded_bytes_required;

    if (current_mode == HybridPacketMode::FixedLengthTail) {
        decision.reason = PacketLengthDecisionReason::AlreadyFixedLength;
        return decision;
    }

    if (!progress.l_field_known || progress.exact_encoded_bytes_required == 0U) {
        decision.reason = PacketLengthDecisionReason::LengthUnknown;
        return decision;
    }

    if (progress.encoded_bytes_seen >= progress.exact_encoded_bytes_required) {
        decision.reason = PacketLengthDecisionReason::NoRemainingBytes;
        return decision;
    }

    decision.remaining_encoded_length = static_cast<uint16_t>(progress.exact_encoded_bytes_required -
                                                              progress.encoded_bytes_seen);
    if (decision.remaining_encoded_length > 0xFFU) {
        decision.reason = PacketLengthDecisionReason::RemainingExceedsHardwareLimit;
        return decision;
    }

    decision.should_switch = true;
    decision.desired_mode = HybridPacketMode::FixedLengthTail;
    decision.reason = PacketLengthDecisionReason::SafeToSwitch;
    decision.fixed_length_register_value =
        static_cast<uint8_t>(decision.remaining_encoded_length);
    return decision;
}

uint16_t exact_read_window_bytes(const CandidateProgress& progress, uint16_t fifo_bytes_available) {
    if (!progress.l_field_known || progress.exact_encoded_bytes_required == 0U) {
        return fifo_bytes_available;
    }
    if (progress.encoded_bytes_seen >= progress.exact_encoded_bytes_required) {
        return 0U;
    }
    const uint16_t remaining =
        static_cast<uint16_t>(progress.exact_encoded_bytes_required - progress.encoded_bytes_seen);
    return std::min<uint16_t>(fifo_bytes_available, remaining);
}

} // namespace wmbus_tmode_rx
