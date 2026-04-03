#pragma once

#include "common/result.hpp"
#include "wmbus_tmode_rx/wmbus_tmode_framer.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace wmbus_link {

enum class ExactFrameRejectReason : uint8_t {
    None = 0,
    InvalidLength,
    InvalidOrientation,
    InvalidFirstBlock,
};

enum class LinkRejectReason : uint8_t {
    None = 0,
    FrameTooShort,
    DecodedLengthMismatch,
    FirstBlockValidationFailed,
    BlockValidationFailed,
    IdentityUnavailable,
};

struct EncodedRxFrameMetadata {
    int8_t rssi_dbm = 0;
    uint8_t lqi = 0;
    bool crc_ok = false;
    bool radio_crc_available = false;
    bool exact_frame_contract_valid = false;
    bool transitional_raw_adapter_used = false;
    int64_t timestamp_ms = 0;
    uint32_t rx_count = 0;
    uint16_t capture_elapsed_ms = 0;
    uint16_t captured_frame_length = 0;
    uint8_t first_data_byte = 0;
    uint8_t burst_end_reason = 0;
};

struct EncodedRxFrame {
    static constexpr size_t kMaxEncodedBytes =
        wmbus_tmode_rx::ExactEncodedFrameCandidate::kMaxEncodedBytes;
    static constexpr size_t kMaxDecodedBytes =
        wmbus_tmode_rx::ExactEncodedFrameCandidate::kMaxDecodedBytes;

    std::array<uint8_t, kMaxEncodedBytes> encoded_bytes{};
    std::array<uint8_t, kMaxDecodedBytes> decoded_bytes{};
    uint16_t encoded_length = 0;
    uint16_t decoded_length = 0;
    uint16_t exact_encoded_bytes_required = 0;
    uint8_t l_field = 0;
    wmbus_tmode_rx::FrameOrientation orientation = wmbus_tmode_rx::FrameOrientation::Unknown;
    wmbus_tmode_rx::FirstBlockValidationState first_block_validation =
        wmbus_tmode_rx::FirstBlockValidationState::NotReady;
    EncodedRxFrameMetadata metadata{};
};

struct ExactFrameValidationResult {
    bool accepted = false;
    ExactFrameRejectReason reject_reason = ExactFrameRejectReason::None;
};

struct LinkTelegramMetadata {
    int8_t rssi_dbm = 0;
    uint8_t lqi = 0;
    bool crc_ok = false;
    bool radio_crc_available = false;
    bool exact_frame_contract_valid = false;
    bool transitional_raw_adapter_used = false;
    int64_t timestamp_ms = 0;
    uint32_t rx_count = 0;
    uint16_t encoded_length = 0;
    uint16_t exact_encoded_bytes_required = 0;
    uint16_t canonical_length = 0;
    wmbus_tmode_rx::FrameOrientation orientation = wmbus_tmode_rx::FrameOrientation::Unknown;
};

struct LinkTelegram {
    static constexpr size_t kMaxCanonicalBytes = EncodedRxFrame::kMaxDecodedBytes;

    std::array<uint8_t, kMaxCanonicalBytes> canonical_bytes{};
    uint16_t manufacturer_id = 0;
    uint32_t device_id = 0;
    bool reliable_identity = false;
    LinkTelegramMetadata metadata{};

    std::string canonical_hex() const;
    std::string dedup_key() const;
    std::string identity_key() const;
    std::string signature_prefix_hex(size_t max_bytes = 12) const;
};

struct ValidatedTelegram {
    EncodedRxFrame exact_frame{};
    LinkTelegram link{};

    std::string canonical_hex() const;
    std::string captured_hex() const;
    std::string dedup_key() const;
    std::string identity_key() const;
    std::string signature_prefix_hex(size_t max_bytes = 12) const;
    uint16_t manufacturer_id() const;
    uint32_t device_id() const;
    bool has_reliable_identity() const;
};

struct LinkValidationResult {
    bool accepted = false;
    LinkRejectReason reject_reason = LinkRejectReason::None;
    ValidatedTelegram telegram{};
};

class WmbusLink {
  public:
    static ExactFrameValidationResult validate_exact_frame(const EncodedRxFrame& frame);
    static LinkValidationResult validate_and_build(const EncodedRxFrame& frame);

    static std::string bytes_to_hex(const uint8_t* data, size_t length);
};

} // namespace wmbus_link
