#pragma once

#include "common/error.hpp"
#include "radio_cc1101/cc1101_owner_events.hpp"
#include "wmbus_prios_rx/prios_capture_service.hpp"
#include "wmbus_tmode_rx/rx_session_engine.hpp"

#include <array>
#include <cstdint>

// PriosBringUpSession: minimal raw-capture session for PRIOS bring-up.
//
// When the radio is configured for the PRIOS R3 profile, the T-mode
// RxSessionEngine cannot be used (it contains the T-mode 3-of-6 framer).
// This class provides a minimal replacement for the PRIOS bring-up phase:
//   - reads FIFO bytes through the same SessionRadio interface
//   - accumulates bytes up to kMaxCaptureBytes
//   - finalises the capture after the budget is reached or after an
//     idle timeout (no new bytes for kIdleTimeoutMs)
//   - stores a PriosCaptureRecord in PriosCaptureService
//
// This is NOT a framer or a decoder. It exists only to gather raw data.
//
// Design notes:
//   - Uses wmbus_tmode_rx::SessionRadio (shared hardware interface).
//   - Compact ESP_LOG calls for bring-up visibility (INFO-level, rate-limited).
//   - No FreeRTOS primitives; only the radio task calls this.
//   - Once the IProtocolDriver path is wired to the session engine (migration
//     step 4), PriosCaptureDriver.feed_byte() will replace this class.

namespace wmbus_prios_rx {

class PriosBringUpSession {
  public:
    // Maximum raw bytes accumulated per capture attempt.
    static constexpr uint16_t kMaxCaptureBytes = 64;

    // Maximum bytes read from FIFO in a single process() call to bound latency.
    static constexpr uint16_t kMaxFifoReadPerCall = 32;

    // If no new bytes arrive for this long, finalise the partial capture.
    static constexpr uint32_t kIdleTimeoutMs = 300;
    static constexpr uint32_t kSummaryLogCadenceMs = 3000;
    static constexpr uint32_t kOverflowLogCadenceMs = 2000;
    static constexpr uint32_t kVerboseSessionLogBudget = 3;
    static constexpr uint16_t kVariantBMinTimeoutCaptureBytes = 16;

    enum class CaptureDecision : uint8_t {
        Accept = 0,
        RejectVariantBShortTimeout,
    };

    struct Result {
        bool                  has_capture  = false;
        PriosCaptureRecord    record{};
        common::ErrorCode     radio_error  = common::ErrorCode::OK;
        bool                  is_fallback_wake = false;
    };

    void reset();

    // Set the capture variant before starting a campaign session.
    // This must be called whenever the operator switches variants so that
    // every new PriosCaptureRecord carries the correct manchester_enabled flag.
    // Safe to call at any time; takes effect on the next capture.
    void set_variant(bool manchester_enabled) {
        manchester_enabled_ = manchester_enabled;
        counters_ = SummaryCounters{};
        last_summary_log_ms_ = 0;
        last_overflow_log_ms_ = 0;
        verbose_session_logs_remaining_ = kVerboseSessionLogBudget;
    }

    // Returns the currently active variant.
    bool manchester_enabled() const { return manchester_enabled_; }
    static CaptureDecision classify_candidate(bool manchester_enabled,
                                              uint16_t captured_bytes,
                                              bool timed_out) {
        if (manchester_enabled && timed_out &&
            captured_bytes < kVariantBMinTimeoutCaptureBytes) {
            return CaptureDecision::RejectVariantBShortTimeout;
        }
        return CaptureDecision::Accept;
    }

    // Called from the radio owner task loop in place of RxSessionEngine::process()
    // when the active profile is WMbusPriosR3.
    //
    // Parameters:
    //   radio         – the same SessionRadio adapter used by T-mode
    //   events        – owner-task notification bitmask
    //   now_ms        – current monotonic timestamp (from xTaskGetTickCount)
    //   timestamp_ms  – NTP epoch timestamp for the record (0 if unavailable)
    Result process(wmbus_tmode_rx::SessionRadio& radio,
                   const radio_cc1101::RadioOwnerEventSet& events,
                   uint32_t now_ms,
                   int64_t  timestamp_ms);

  private:
    static constexpr size_t kBufSize = kMaxCaptureBytes;

    struct SummaryCounters {
        uint32_t sessions_started = 0;
        uint32_t captures_completed = 0;
        uint32_t fifo_overflows = 0;
        uint32_t timeout_captures = 0;
        uint32_t empty_resets = 0;
        uint32_t fallback_wakes = 0;
        uint32_t noise_rejections = 0;
        uint32_t variant_b_short_rejections = 0;
    };

    uint8_t  buf_[kBufSize]{};
    uint16_t len_               = 0;
    bool     session_active_    = false;
    uint32_t session_start_ms_  = 0;
    uint32_t last_byte_ms_      = 0;
    uint32_t seq_               = 0;
    bool     manchester_enabled_ = false;  // set by set_variant(); propagated to records
    SummaryCounters counters_{};
    uint32_t last_summary_log_ms_ = 0;
    uint32_t last_overflow_log_ms_ = 0;
    uint32_t verbose_session_logs_remaining_ = kVerboseSessionLogBudget;

    PriosCaptureRecord finalise(int8_t rssi_dbm, uint8_t lqi,
                                bool crc_ok, bool crc_available,
                                int64_t timestamp_ms);
    bool should_emit_verbose_session_log() const;
    void emit_periodic_summary(uint32_t now_ms);
};

} // namespace wmbus_prios_rx
