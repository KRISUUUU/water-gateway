#pragma once

#include "common/error.hpp"
#include "common/result.hpp"
#include "radio_cc1101/cc1101_owner_events.hpp"
#include "rf_diagnostics/rf_diagnostics.hpp"
#include "wmbus_tmode_rx/packet_length_strategy.hpp"
#include "wmbus_tmode_rx/wmbus_tmode_framer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace wmbus_tmode_rx {

struct SessionRadioStatus {
    uint8_t fifo_bytes = 0;
    bool fifo_overflow = false;
    bool receiving = false;
};

struct SessionSignalQuality {
    int8_t rssi_dbm = 0;
    uint8_t lqi = 0;
    bool crc_ok = false;
    bool radio_crc_available = false;
};

class SessionRadio {
  public:
    virtual ~SessionRadio() = default;

    virtual common::Result<SessionRadioStatus> read_status() = 0;
    virtual common::Result<uint16_t> read_fifo(uint8_t* buffer, uint16_t capacity) = 0;
    virtual common::Result<SessionSignalQuality> read_signal_quality() = 0;
    virtual common::Result<void> switch_to_fixed_length(uint8_t remaining_encoded_bytes) = 0;
    virtual common::Result<void> restore_infinite_packet_mode() = 0;
    virtual common::Result<void> abort_and_restart_rx() = 0;
};

enum class SessionStepState : uint8_t {
    Idle = 0,
    SessionInProgress,
    ExactFrameComplete,
    DiagnosticReady,
    RecoveryRequested,
};

enum class SessionAbortReason : uint8_t {
    None = 0,
    CandidateRejected,
    NoProgressTimeout,
    RadioOverflow,
    RadioSpiFailure,
};

struct SessionEngineConfig {
    uint32_t idle_poll_timeout_ms = 8;
    uint32_t min_watchdog_timeout_ms = 6;
    uint32_t post_l_field_watchdog_floor_ms = 10;
};

struct SessionProgressSnapshot {
    bool active = false;
    uint32_t started_at_ms = 0;
    uint32_t last_progress_ms = 0;
    uint16_t encoded_bytes_seen = 0;
    uint16_t decoded_bytes_seen = 0;
    uint16_t expected_encoded_length = 0;
    uint16_t expected_decoded_length = 0;
    HybridPacketMode packet_mode = HybridPacketMode::Infinite;
    FrameOrientation orientation = FrameOrientation::Unknown;
    bool l_field_known = false;
};

struct SessionFrameCapture {
    ExactEncodedFrameCandidate candidate{};
    int8_t rssi_dbm = 0;
    uint8_t lqi = 0;
    bool crc_ok = false;
    bool radio_crc_available = false;
    uint16_t capture_elapsed_ms = 0;
    uint8_t first_data_byte = 0;
};

struct SessionStepResult {
    SessionStepState state = SessionStepState::Idle;
    common::ErrorCode radio_error = common::ErrorCode::OK;
    SessionAbortReason abort_reason = SessionAbortReason::None;
    SessionProgressSnapshot progress{};
    bool has_frame = false;
    SessionFrameCapture frame{};
    bool has_diagnostic = false;
    rf_diagnostics::RfDiagnosticRecord diagnostic{};
};

class RxSessionEngine {
  public:
    explicit RxSessionEngine(const SessionEngineConfig& config = {});

    void reset();
    SessionProgressSnapshot snapshot() const;

    common::Result<SessionStepResult> process(SessionRadio& radio,
                                              const radio_cc1101::RadioOwnerEventSet& events,
                                              uint32_t now_ms);

    static uint32_t no_progress_timeout_ms(const SessionEngineConfig& config,
                                           const CandidateProgress& progress);

  private:
    static constexpr size_t kCapturedPrefixBytes =
        rf_diagnostics::RfDiagnosticRecord::kMaxCapturedPrefixBytes;

    common::Result<SessionStepResult> process_impl(SessionRadio& radio,
                                                   const radio_cc1101::RadioOwnerEventSet& events,
                                                   uint32_t now_ms);
    common::Result<void> maybe_switch_to_fixed_length(SessionRadio& radio,
                                                      const CandidateProgress& progress);
    common::Result<SessionStepResult> start_recovery(SessionRadio& radio, SessionAbortReason reason,
                                                     common::ErrorCode radio_error,
                                                     uint32_t now_ms);
    SessionStepResult make_idle_result() const;
    SessionStepResult make_progress_result(SessionStepState state) const;
    SessionStepResult make_diagnostic_result(SessionAbortReason abort_reason,
                                             rf_diagnostics::RejectReason reject_reason,
                                             uint32_t now_ms) const;
    SessionStepResult make_complete_frame_result(const FeedResult& feed_result,
                                                 const SessionSignalQuality& quality,
                                                 uint32_t now_ms) const;
    void begin_session(uint32_t now_ms);
    void clear_session();
    void record_captured_byte(uint8_t byte);
    bool watchdog_expired(uint32_t now_ms) const;
    rf_diagnostics::Orientation diagnostic_orientation(const FeedResult& feed_result) const;
    rf_diagnostics::RejectReason map_reject_reason(RejectReason reason) const;

    SessionEngineConfig config_{};
    WmbusTmodeFramer framer_{};
    bool session_active_ = false;
    uint32_t session_started_at_ms_ = 0;
    uint32_t last_progress_ms_ = 0;
    uint8_t first_data_byte_ = 0;
    bool first_data_byte_valid_ = false;
    uint16_t encoded_bytes_seen_ = 0;
    HybridPacketMode packet_mode_ = HybridPacketMode::Infinite;
    std::array<uint8_t, kCapturedPrefixBytes> captured_prefix_{};
    uint8_t captured_prefix_length_ = 0;
    FeedResult last_feed_result_{};
};

} // namespace wmbus_tmode_rx
