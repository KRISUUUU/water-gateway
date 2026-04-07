#pragma once

#include "common/error.hpp"
#include "protocol_driver/protocol_ids.hpp"
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
    enum class Mode : uint8_t {
        SyncCampaign = 0,
        DiscoverySniffer,
    };

    // Maximum raw bytes accumulated per capture attempt.
    static constexpr uint16_t kMaxCaptureBytes = 128;

    // Maximum bytes read from FIFO in a single process() call to bound latency.
    static constexpr uint16_t kMaxFifoReadPerCall = 32;

    // If no new bytes arrive for this long, finalise the partial capture.
    static constexpr uint32_t kIdleTimeoutMs = 300;
    static constexpr uint32_t kSummaryLogCadenceMs = 3000;
    static constexpr uint32_t kOverflowLogCadenceMs = 2000;
    static constexpr uint32_t kVerboseSessionLogBudget = 3;
    static constexpr uint16_t kVariantBMinTimeoutCaptureBytes = 12;
    static constexpr int8_t kDiscoveryMinRssiDbm = -96;
    static constexpr uint8_t kDiscoveryMinLqi = 16;

    enum class CaptureDecision : uint8_t {
        Accept = 0,
        RejectVariantBShortTimeout,
        RejectDiscoveryWeakSignal,
    };

    struct Result {
        bool                  has_capture  = false;
        PriosCaptureRecord    record{};
        common::ErrorCode     radio_error  = common::ErrorCode::OK;
        bool                  is_fallback_wake = false;
    };

    void reset();
    bool active() const { return session_active_; }
    void note_noise_rejection() { counters_.noise_rejections++; }

    void configure(Mode mode, bool manchester_enabled,
                   protocol_driver::RadioProfileId radio_profile) {
        mode_ = mode;
        manchester_enabled_ = manchester_enabled;
        radio_profile_ = radio_profile;
        counters_ = SummaryCounters{};
        last_summary_log_ms_ = 0;
        last_overflow_log_ms_ = 0;
        verbose_session_logs_remaining_ = kVerboseSessionLogBudget;
    }

    void set_variant(bool manchester_enabled) {
        configure(mode_, manchester_enabled, radio_profile_);
    }

    Mode mode() const { return mode_; }

    // Returns the currently active variant.
    bool manchester_enabled() const { return manchester_enabled_; }
    static CaptureDecision classify_candidate(Mode mode,
                                              bool manchester_enabled,
                                              uint16_t captured_bytes,
                                              bool timed_out,
                                              int8_t rssi_dbm,
                                              uint8_t lqi) {
        if (mode == Mode::SyncCampaign &&
            manchester_enabled && timed_out &&
            captured_bytes < kVariantBMinTimeoutCaptureBytes) {
            return CaptureDecision::RejectVariantBShortTimeout;
        }
        if (mode == Mode::DiscoverySniffer &&
            (rssi_dbm < kDiscoveryMinRssiDbm || lqi < kDiscoveryMinLqi)) {
            return CaptureDecision::RejectDiscoveryWeakSignal;
        }
        return CaptureDecision::Accept;
    }

    static bool should_start_capture(Mode mode,
                                     const radio_cc1101::RadioOwnerEventSet& events,
                                     const wmbus_tmode_rx::SessionRadioStatus& status) {
        if (mode == Mode::DiscoverySniffer) {
            return events.has_any_irq() || status.fifo_bytes > 0;
        }
        return status.fifo_bytes > 0 || status.receiving;
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
        uint32_t quality_rejections = 0;
        uint32_t variant_b_short_rejections = 0;
    };

    uint8_t  buf_[kBufSize]{};
    uint16_t len_               = 0;
    bool     session_active_    = false;
    uint32_t session_start_ms_  = 0;
    uint32_t last_byte_ms_      = 0;
    uint32_t seq_               = 0;
    Mode     mode_              = Mode::SyncCampaign;
    bool     manchester_enabled_ = false;  // set by set_variant(); propagated to records
    protocol_driver::RadioProfileId radio_profile_ =
        protocol_driver::RadioProfileId::WMbusPriosR3;
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
