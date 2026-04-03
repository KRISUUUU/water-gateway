#include "wmbus_tmode_rx/wmbus_tmode_framer.hpp"

namespace wmbus_tmode_rx {

namespace {
constexpr uint8_t kInvalidNibble = 0xFF;

constexpr uint8_t kDecode3of6High[64] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x30, 0xFF, 0x10, 0x20,
    0xFF, 0xFF, 0xFF, 0xFF, 0x70, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0x50, 0x60, 0xFF, 0x40, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xB0, 0xFF, 0x90, 0xA0, 0xFF, 0xFF, 0xF0, 0xFF, 0xFF, 0x80,
    0xFF, 0xFF, 0xFF, 0xFF, 0xD0, 0xE0, 0xFF, 0xC0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
};

constexpr uint8_t kDecode3of6Low[64] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x03, 0xFF, 0x01, 0x02,
    0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0x05, 0x06, 0xFF, 0x04, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0B, 0xFF, 0x09, 0x0A, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF, 0x08,
    0xFF, 0xFF, 0xFF, 0xFF, 0x0D, 0x0E, 0xFF, 0x0C, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
};
} // namespace

WmbusTmodeFramer::WmbusTmodeFramer() {
    reset();
}

void WmbusTmodeFramer::reset() {
    encoded_length_ = 0;
    encoded_bytes_.fill(0);
    normal_ = {};
    reversed_ = {};
    normal_.progress.orientation = FrameOrientation::Normal;
    reversed_.progress.orientation = FrameOrientation::BitReversed;
}

FeedResult WmbusTmodeFramer::feed_byte(uint8_t encoded_byte) {
    if (encoded_length_ >= kMaxEncodedBytes) {
        reject_candidate(normal_, RejectReason::ProgressExceeded);
        reject_candidate(reversed_, RejectReason::ProgressExceeded);
        return make_result();
    }

    encoded_bytes_[encoded_length_++] = encoded_byte;
    process_candidate(normal_, encoded_byte);
    process_candidate(reversed_, reverse_bits8(encoded_byte));
    return make_result();
}

FeedResult WmbusTmodeFramer::make_result() const {
    FeedResult result{};
    result.normal = normal_.progress;
    result.reversed = reversed_.progress;

    const bool normal_complete = candidate_is_complete(normal_);
    const bool reversed_complete = candidate_is_complete(reversed_);

    if (normal_complete && reversed_complete) {
        bool identical = normal_.progress.expected_decoded_bytes == reversed_.progress.expected_decoded_bytes;
        if (identical) {
            for (uint16_t i = 0; i < normal_.progress.expected_decoded_bytes; ++i) {
                if (normal_.decoded_bytes[i] != reversed_.decoded_bytes[i]) {
                    identical = false;
                    break;
                }
            }
        }
        if (!identical) {
            result.state = FramerState::CandidateRejected;
            result.reject_reason = RejectReason::AmbiguousOrientation;
            return result;
        }
    }

    if (copy_complete_frame(result)) {
        result.state = FramerState::ExactFrameComplete;
        result.has_complete_frame = true;
        return result;
    }

    if (!normal_.progress.active && !reversed_.progress.active) {
        result.state = FramerState::CandidateRejected;
        result.reject_reason = normal_.progress.reject_reason != RejectReason::None
                                   ? normal_.progress.reject_reason
                                   : reversed_.progress.reject_reason;
        return result;
    }

    if (normal_.progress.decoded_bytes_produced > 0U || reversed_.progress.decoded_bytes_produced > 0U ||
        normal_.progress.l_field_known || reversed_.progress.l_field_known) {
        result.state = FramerState::CandidateViable;
    } else {
        result.state = FramerState::NeedMoreData;
    }
    return result;
}

void WmbusTmodeFramer::process_candidate(InternalCandidate& candidate, uint8_t transformed_byte) {
    if (!candidate.progress.active) {
        return;
    }

    candidate.progress.encoded_bytes_seen++;
    candidate.bit_buffer = (candidate.bit_buffer << 8U) | transformed_byte;
    candidate.pending_bits = static_cast<uint8_t>(candidate.pending_bits + 8U);

    while (candidate.pending_bits >= 12U && candidate.progress.active) {
        const uint16_t symbol_pair = static_cast<uint16_t>(
            (candidate.bit_buffer >> (candidate.pending_bits - 12U)) & 0x0FFFU);
        candidate.pending_bits = static_cast<uint8_t>(candidate.pending_bits - 12U);

        const uint8_t decoded = decode_symbol_pair(static_cast<uint8_t>((symbol_pair >> 6U) & 0x3FU),
                                                   static_cast<uint8_t>(symbol_pair & 0x3FU));
        if (decoded == kInvalidNibble) {
            reject_candidate(candidate, RejectReason::InvalidSymbol);
            return;
        }

        if (candidate.progress.decoded_bytes_produced >= kMaxDecodedBytes) {
            reject_candidate(candidate, RejectReason::ProgressExceeded);
            return;
        }

        candidate.decoded_bytes[candidate.progress.decoded_bytes_produced++] = decoded;

        if (!candidate.progress.l_field_known) {
            candidate.progress.l_field = decoded;
            candidate.progress.expected_decoded_bytes = calculate_format_a_decoded_length(decoded);
            candidate.progress.exact_encoded_bytes_required =
                encoded_bytes_for_decoded_length(candidate.progress.expected_decoded_bytes);
            candidate.progress.l_field_known = true;

            if (candidate.progress.expected_decoded_bytes < kMinDecodedFrameBytes ||
                candidate.progress.expected_decoded_bytes > kMaxDecodedBytes ||
                candidate.progress.exact_encoded_bytes_required > kMaxEncodedBytes) {
                reject_candidate(candidate, RejectReason::LengthOutOfRange);
                return;
            }
        }

        if (candidate.progress.decoded_bytes_produced > candidate.progress.expected_decoded_bytes) {
            reject_candidate(candidate, RejectReason::ProgressExceeded);
            return;
        }

        const auto block_validation = validate_first_block(
            candidate.decoded_bytes.data(), candidate.progress.decoded_bytes_produced);
        candidate.progress.first_block_validation = block_validation.state;
        if (block_validation.state == FirstBlockValidationState::Failed) {
            reject_candidate(candidate, RejectReason::FirstBlockValidationFailed);
            return;
        }
    }

    if (candidate.progress.active && candidate.progress.l_field_known &&
        candidate.progress.encoded_bytes_seen > candidate.progress.exact_encoded_bytes_required) {
        reject_candidate(candidate, RejectReason::ProgressExceeded);
    }
}

void WmbusTmodeFramer::reject_candidate(InternalCandidate& candidate, RejectReason reason) {
    candidate.progress.active = false;
    candidate.progress.reject_reason = reason;
}

bool WmbusTmodeFramer::candidate_is_complete(const InternalCandidate& candidate) const {
    return candidate.progress.active && candidate.progress.l_field_known &&
           candidate.progress.expected_decoded_bytes > 0U &&
           candidate.progress.decoded_bytes_produced == candidate.progress.expected_decoded_bytes &&
           candidate.progress.encoded_bytes_seen == candidate.progress.exact_encoded_bytes_required &&
           candidate.progress.first_block_validation == FirstBlockValidationState::Passed;
}

bool WmbusTmodeFramer::copy_complete_frame(FeedResult& result) const {
    const InternalCandidate* chosen = nullptr;
    const bool normal_complete = candidate_is_complete(normal_);
    const bool reversed_complete = candidate_is_complete(reversed_);

    if (normal_complete) {
        chosen = &normal_;
    } else if (reversed_complete) {
        chosen = &reversed_;
    }

    if (!chosen) {
        return false;
    }

    result.frame.orientation = chosen->progress.orientation;
    result.frame.l_field = chosen->progress.l_field;
    result.frame.encoded_length = encoded_length_;
    result.frame.decoded_length = chosen->progress.expected_decoded_bytes;
    result.frame.exact_encoded_bytes_required = chosen->progress.exact_encoded_bytes_required;
    result.frame.first_block_validation = chosen->progress.first_block_validation;
    for (uint16_t i = 0; i < encoded_length_; ++i) {
        result.frame.encoded_bytes[i] = encoded_bytes_[i];
    }
    for (uint16_t i = 0; i < chosen->progress.expected_decoded_bytes; ++i) {
        result.frame.decoded_bytes[i] = chosen->decoded_bytes[i];
    }
    return true;
}

uint8_t WmbusTmodeFramer::reverse_bits8(uint8_t value) {
    return static_cast<uint8_t>(((value & 0x01U) << 7U) | ((value & 0x02U) << 5U) |
                                ((value & 0x04U) << 3U) | ((value & 0x08U) << 1U) |
                                ((value & 0x10U) >> 1U) | ((value & 0x20U) >> 3U) |
                                ((value & 0x40U) >> 5U) | ((value & 0x80U) >> 7U));
}

uint8_t WmbusTmodeFramer::decode_symbol_pair(uint8_t hi_symbol, uint8_t lo_symbol) {
    const uint8_t hi = kDecode3of6High[hi_symbol];
    const uint8_t lo = kDecode3of6Low[lo_symbol];
    if (hi == kInvalidNibble || lo == kInvalidNibble) {
        return kInvalidNibble;
    }
    return static_cast<uint8_t>(hi | lo);
}

} // namespace wmbus_tmode_rx
