#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace rf_diagnostics {

enum class RejectReason : uint8_t {
    None = 0,
    OversizedBurst,
    BurstTimeout,
    Invalid3of6Symbol,
    InvalidLength,
    InvalidOrientation,
    DecodedLengthMismatch,
    FirstBlockValidationFailed,
    BlockValidationFailed,
    ExactLengthMismatch,
    RadioOverflow,
    RadioSpiError,
    QualityDrop,
    TransitionalAdapterRejected,
    SessionAborted,
    // Link-layer specific reasons (from wmbus_link downstream validation).
    FrameTooShort,
    IdentityUnavailable,
    Unknown,
};

enum class Orientation : uint8_t {
    Unknown = 0,
    Normal,
    BitReversed,
};

struct RfDiagnosticRecord {
    static constexpr size_t kMaxCapturedPrefixBytes = 24;
    static constexpr size_t kMaxDecodedPrefixBytes = 24;

    uint32_t sequence = 0;
    int64_t timestamp_epoch_ms = 0;
    int64_t monotonic_ms = 0;
    RejectReason reject_reason = RejectReason::None;
    Orientation orientation = Orientation::Unknown;
    uint16_t expected_encoded_length = 0;
    uint16_t actual_encoded_length = 0;
    uint16_t expected_decoded_length = 0;
    uint16_t actual_decoded_length = 0;
    uint16_t capture_elapsed_ms = 0;
    uint8_t first_data_byte = 0;
    bool quality_issue = false;
    bool signal_quality_valid = false;
    int8_t rssi_dbm = 0;
    uint8_t lqi = 0;
    bool radio_crc_available = false;
    bool radio_crc_ok = false;
    std::array<uint8_t, kMaxCapturedPrefixBytes> captured_prefix{};
    uint8_t captured_prefix_length = 0;
    std::array<uint8_t, kMaxDecodedPrefixBytes> decoded_prefix{};
    uint8_t decoded_prefix_length = 0;
};

struct RfDiagnosticsSnapshot {
    static constexpr size_t kMaxRecords = 16;

    std::array<RfDiagnosticRecord, kMaxRecords> records{};
    size_t count = 0;
    uint32_t total_inserted = 0;
    uint32_t total_evicted = 0;
};

class RfDiagnosticsRingBuffer {
  public:
    static constexpr size_t kCapacity = RfDiagnosticsSnapshot::kMaxRecords;

    void clear();
    void insert(const RfDiagnosticRecord& record);
    [[nodiscard]] RfDiagnosticsSnapshot snapshot() const;
    [[nodiscard]] size_t size() const {
        return count_;
    }
    [[nodiscard]] size_t capacity() const {
        return storage_.size();
    }

  private:
    std::array<RfDiagnosticRecord, kCapacity> storage_{};
    size_t head_ = 0;
    size_t count_ = 0;
    uint32_t total_inserted_ = 0;
    uint32_t total_evicted_ = 0;
};

class RfDiagnosticsService {
  public:
    static RfDiagnosticsService& instance();

    void clear();
    void insert(const RfDiagnosticRecord& record);
    [[nodiscard]] RfDiagnosticsSnapshot snapshot() const;
    [[nodiscard]] std::unique_ptr<RfDiagnosticsSnapshot> snapshot_allocated() const;

    [[nodiscard]] static std::string to_json(const RfDiagnosticsSnapshot& snapshot);
    [[nodiscard]] static std::string reject_reason_to_string(RejectReason reason);
    [[nodiscard]] static std::string orientation_to_string(Orientation orientation);
    [[nodiscard]] static std::string prefix_to_hex(const uint8_t* data, size_t length);

  private:
    RfDiagnosticsService() = default;

    mutable std::mutex mutex_{};
    RfDiagnosticsRingBuffer ring_{};
};

} // namespace rf_diagnostics
