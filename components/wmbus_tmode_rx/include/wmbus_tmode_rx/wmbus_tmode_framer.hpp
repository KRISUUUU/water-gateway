#pragma once

#include "wmbus_tmode_rx/wmbus_block_validation.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace wmbus_tmode_rx {

enum class FrameOrientation : uint8_t {
    Unknown = 0,
    Normal,
    BitReversed,
};

enum class FramerState : uint8_t {
    NeedMoreData = 0,
    CandidateViable,
    ExactFrameComplete,
    CandidateRejected,
};

enum class RejectReason : uint8_t {
    None = 0,
    InvalidSymbol,
    LengthOutOfRange,
    ProgressExceeded,
    FirstBlockValidationFailed,
    AmbiguousOrientation,
};

struct CandidateProgress {
    FrameOrientation orientation = FrameOrientation::Unknown;
    bool active = true;
    bool l_field_known = false;
    uint8_t l_field = 0;
    uint16_t encoded_bytes_seen = 0;
    uint16_t decoded_bytes_produced = 0;
    uint16_t exact_encoded_bytes_required = 0;
    uint16_t expected_decoded_bytes = 0;
    FirstBlockValidationState first_block_validation = FirstBlockValidationState::NotReady;
    RejectReason reject_reason = RejectReason::None;
};

struct ExactEncodedFrameCandidate {
    static constexpr size_t kMaxEncodedBytes = 290;
    static constexpr size_t kMaxDecodedBytes = (kMaxEncodedBytes * 8U) / 12U;

    FrameOrientation orientation = FrameOrientation::Unknown;
    uint8_t l_field = 0;
    uint16_t encoded_length = 0;
    uint16_t decoded_length = 0;
    uint16_t exact_encoded_bytes_required = 0;
    FirstBlockValidationState first_block_validation = FirstBlockValidationState::NotReady;
    std::array<uint8_t, kMaxEncodedBytes> encoded_bytes{};
    std::array<uint8_t, kMaxDecodedBytes> decoded_bytes{};
};

struct FeedResult {
    FramerState state = FramerState::NeedMoreData;
    RejectReason reject_reason = RejectReason::None;
    CandidateProgress normal{};
    CandidateProgress reversed{};
    bool has_complete_frame = false;
    ExactEncodedFrameCandidate frame{};
};

class WmbusTmodeFramer {
  public:
    static constexpr size_t kMaxEncodedBytes = ExactEncodedFrameCandidate::kMaxEncodedBytes;
    static constexpr size_t kMaxDecodedBytes = ExactEncodedFrameCandidate::kMaxDecodedBytes;
    static constexpr uint16_t kMinDecodedFrameBytes = 12;

    WmbusTmodeFramer();

    void reset();

    FeedResult feed_byte(uint8_t encoded_byte);

    static constexpr uint16_t encoded_bytes_for_decoded_length(uint16_t decoded_length) {
        return static_cast<uint16_t>((static_cast<uint32_t>(decoded_length) * 12U + 7U) / 8U);
    }

  private:
    struct InternalCandidate {
        CandidateProgress progress{};
        uint32_t bit_buffer = 0;
        uint8_t pending_bits = 0;
        std::array<uint8_t, kMaxDecodedBytes> decoded_bytes{};
    };

    FeedResult make_result() const;
    void process_candidate(InternalCandidate& candidate, uint8_t transformed_byte);
    void reject_candidate(InternalCandidate& candidate, RejectReason reason);
    bool candidate_is_complete(const InternalCandidate& candidate) const;
    bool copy_complete_frame(FeedResult& result) const;

    static uint8_t reverse_bits8(uint8_t value);
    static uint16_t decode_symbol_pair(uint8_t hi_symbol, uint8_t lo_symbol);

    uint16_t encoded_length_ = 0;
    std::array<uint8_t, kMaxEncodedBytes> encoded_bytes_{};
    InternalCandidate normal_{};
    InternalCandidate reversed_{};
};

} // namespace wmbus_tmode_rx
