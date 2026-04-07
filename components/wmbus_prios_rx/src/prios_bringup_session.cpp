#include "wmbus_prios_rx/prios_bringup_session.hpp"

#include <algorithm>
#include <cstring>

#ifndef HOST_TEST_BUILD
#include "esp_log.h"
static const char* TAG_PRIOS = "prios_bringup";
#else
#define ESP_LOGI(tag, fmt, ...) (void)0
#define ESP_LOGW(tag, fmt, ...) (void)0
#define ESP_LOGD(tag, fmt, ...) (void)0
#endif

namespace wmbus_prios_rx {

using namespace wmbus_tmode_rx;
using namespace radio_cc1101;

bool PriosBringUpSession::should_emit_verbose_session_log() const {
    return verbose_session_logs_remaining_ > 0;
}

void PriosBringUpSession::emit_periodic_summary(uint32_t now_ms) {
#ifndef HOST_TEST_BUILD
    if (last_summary_log_ms_ == 0) {
        last_summary_log_ms_ = now_ms;
        return;
    }
    if ((now_ms - last_summary_log_ms_) < kSummaryLogCadenceMs) {
        return;
    }
    last_summary_log_ms_ = now_ms;

    const auto stats = PriosCaptureService::instance().stats();
    const char* profile_label =
        radio_profile_ == protocol_driver::RadioProfileId::WMbusPriosR4
            ? "PRIOS R4"
            : "PRIOS R3";
    const char* mode_label =
        mode_ == Mode::DiscoverySniffer ? "PRIOS discovery summary" : profile_label;
    ESP_LOGI(TAG_PRIOS,
             "%s: start=%lu capture=%lu timeout=%lu reject_noise=%lu reject_quality=%lu reject_b_short=%lu reject_sim=%lu overflow=%lu empty=%lu fallback=%lu stored=%lu evict=%lu",
             mode_label,
             static_cast<unsigned long>(counters_.sessions_started),
             static_cast<unsigned long>(counters_.captures_completed),
             static_cast<unsigned long>(counters_.timeout_captures),
             static_cast<unsigned long>(counters_.noise_rejections),
             static_cast<unsigned long>(counters_.quality_rejections),
             static_cast<unsigned long>(counters_.variant_b_short_rejections),
             static_cast<unsigned long>(stats.total_similarity_rejected),
             static_cast<unsigned long>(counters_.fifo_overflows),
             static_cast<unsigned long>(counters_.empty_resets),
             static_cast<unsigned long>(counters_.fallback_wakes),
             static_cast<unsigned long>(stats.total_inserted),
             static_cast<unsigned long>(stats.total_evicted));
#else
    (void)now_ms;
#endif
}

void PriosBringUpSession::reset() {
    len_            = 0;
    session_active_ = false;
    session_start_ms_ = 0;
    last_byte_ms_   = 0;
    // seq_ intentionally NOT reset — monotonically increasing across sessions
}

PriosCaptureRecord PriosBringUpSession::finalise(int8_t rssi_dbm, uint8_t lqi,
                                                  bool crc_ok, bool crc_available,
                                                  int64_t timestamp_ms) {
    PriosCaptureRecord rec{};
    rec.sequence              = ++seq_;
    rec.timestamp_ms          = timestamp_ms;
    rec.rssi_dbm              = rssi_dbm;
    rec.lqi                   = lqi;
    rec.radio_crc_ok          = crc_ok;
    rec.radio_crc_available   = crc_available;
    rec.total_bytes_captured  = len_;
    rec.radio_profile         = radio_profile_;
    rec.manchester_enabled    = manchester_enabled_;  // record which variant was active

    const size_t captured_length = std::min(static_cast<size_t>(len_),
                                            PriosCaptureRecord::kMaxCaptureBytes);
    std::memcpy(rec.captured_bytes, buf_, captured_length);

    return rec;
}

PriosBringUpSession::Result PriosBringUpSession::process(
    SessionRadio& radio,
    const RadioOwnerEventSet& events,
    uint32_t now_ms,
    int64_t  timestamp_ms) {

    Result result{};
    result.is_fallback_wake = events.has(RadioOwnerEvent::FallbackPoll) &&
                              !events.has_any_irq();
    if (result.is_fallback_wake) {
        counters_.fallback_wakes++;
    }
    emit_periodic_summary(now_ms);

    // Skip FIFO read if there's no reason to expect work.
    if (!events.should_attempt_rx_work(session_active_)) {
        return result;
    }

    // Read status to decide whether to poll FIFO.
    auto status_res = radio.read_status();
    if (status_res.is_error()) {
        result.radio_error = status_res.error();
        reset();
        return result;
    }
    const auto& status = status_res.value();

    if (!session_active_ && status.fifo_bytes == 0 && !status.receiving &&
        !events.has_any_irq()) {
        return result; // Nothing to capture yet.
    }

    // Begin a new session once the current mode's trigger conditions are met.
    if (!session_active_ && should_start_capture(mode_, events, status)) {
        session_active_   = true;
        session_start_ms_ = now_ms;
        last_byte_ms_     = now_ms;
        counters_.sessions_started++;
        PriosCaptureService::instance().record_burst_start();
        if (mode_ == Mode::SyncCampaign) {
            PriosCaptureService::instance().record_sync_campaign_start();
        }
        if (should_emit_verbose_session_log()) {
            ESP_LOGD(TAG_PRIOS,
                     "PRIOS session start: mode=%s variant=%s fallback=%d",
                     mode_ == Mode::DiscoverySniffer ? "discovery" : "campaign",
                     manchester_enabled_ ? "manchester_on" : "manchester_off",
                     static_cast<int>(result.is_fallback_wake));
        }
    }

    // Drain available FIFO bytes, up to per-call limit.
    if (status.fifo_bytes > 0) {
        const uint16_t remaining_budget =
            static_cast<uint16_t>(kMaxCaptureBytes - len_);
        const uint16_t to_read =
            std::min<uint16_t>({static_cast<uint16_t>(status.fifo_bytes),
                                kMaxFifoReadPerCall,
                                remaining_budget});
        if (to_read > 0) {
            uint8_t tmp[kMaxFifoReadPerCall];
            auto read_res = radio.read_fifo(tmp, to_read);
            if (read_res.is_error()) {
                result.radio_error = read_res.error();
                reset();
                return result;
            }
            const uint16_t got = read_res.value();
            const uint16_t copy = std::min<uint16_t>(got, remaining_budget);
            if (copy > 0) {
                if (len_ == 0 && copy >= 1 && should_emit_verbose_session_log()) {
                    if (copy >= 2) {
                        ESP_LOGD(TAG_PRIOS, "PRIOS first bytes: %02X %02X",
                                 tmp[0], tmp[1]);
                    } else {
                        ESP_LOGD(TAG_PRIOS, "PRIOS first byte: %02X", tmp[0]);
                    }
                }
                std::memcpy(buf_ + len_, tmp, copy);
                len_ += copy;
                last_byte_ms_ = now_ms;
            }
        }
    }

    if (status.fifo_overflow) {
        // FIFO overflowed; finalise what we have and restart.
        counters_.fifo_overflows++;
        if (last_overflow_log_ms_ == 0 ||
            (now_ms - last_overflow_log_ms_) >= kOverflowLogCadenceMs) {
            last_overflow_log_ms_ = now_ms;
            ESP_LOGW(TAG_PRIOS, "PRIOS overflow: bytes=%u sessions=%lu captures=%lu",
                     len_,
                     static_cast<unsigned long>(counters_.sessions_started),
                     static_cast<unsigned long>(counters_.captures_completed));
        }
        if (len_ > 0) {
            // Read signal quality before discarding.
            int8_t rssi = 0; uint8_t lqi = 0; bool crc_ok = false; bool crc_avail = false;
            auto sq = radio.read_signal_quality();
            if (sq.is_ok()) {
                rssi = sq.value().rssi_dbm; lqi = sq.value().lqi;
                crc_ok = sq.value().crc_ok; crc_avail = sq.value().radio_crc_available;
            }
            result.record      = finalise(rssi, lqi, crc_ok, crc_avail, timestamp_ms);
            result.has_capture = true;
            counters_.captures_completed++;
        }
        if (verbose_session_logs_remaining_ > 0) {
            verbose_session_logs_remaining_--;
        }
        radio.abort_and_restart_rx();
        reset();
        return result;
    }

    // Budget exhausted → capture complete.
    if (len_ >= kMaxCaptureBytes) {
        auto sq = radio.read_signal_quality();
        int8_t rssi = 0; uint8_t lqi = 0; bool crc_ok = false; bool crc_avail = false;
        if (sq.is_ok()) {
            rssi = sq.value().rssi_dbm; lqi = sq.value().lqi;
            crc_ok = sq.value().crc_ok; crc_avail = sq.value().radio_crc_available;
        }
        const auto decision = classify_candidate(mode_, manchester_enabled_, len_, false, rssi, lqi);
        if (decision == CaptureDecision::RejectDiscoveryWeakSignal) {
            counters_.noise_rejections++;
            counters_.quality_rejections++;
            PriosCaptureService::instance().record_quality_rejection();
            PriosCaptureService::instance().record_noise_rejection(manchester_enabled_, false);
            if (verbose_session_logs_remaining_ > 0) {
                verbose_session_logs_remaining_--;
            }
            radio.abort_and_restart_rx();
            reset();
            return result;
        }
        if (should_emit_verbose_session_log()) {
            ESP_LOGD(TAG_PRIOS,
                     "PRIOS capture complete: %u bytes rssi=%d lqi=%u prefix=%02X %02X %02X %02X",
                     len_, rssi, lqi,
                     len_ > 0 ? buf_[0] : 0u,
                     len_ > 1 ? buf_[1] : 0u,
                     len_ > 2 ? buf_[2] : 0u,
                     len_ > 3 ? buf_[3] : 0u);
        }
        result.record      = finalise(rssi, lqi, crc_ok, crc_avail, timestamp_ms);
        result.has_capture = true;
        counters_.captures_completed++;
        if (verbose_session_logs_remaining_ > 0) {
            verbose_session_logs_remaining_--;
        }
        radio.abort_and_restart_rx();
        reset();
        return result;
    }

    // Idle timeout → finalise partial capture if we have any bytes.
    if (session_active_ && len_ > 0 &&
        (now_ms - last_byte_ms_) >= kIdleTimeoutMs) {
        auto sq = radio.read_signal_quality();
        int8_t rssi = 0; uint8_t lqi = 0; bool crc_ok = false; bool crc_avail = false;
        if (sq.is_ok()) {
            rssi = sq.value().rssi_dbm; lqi = sq.value().lqi;
            crc_ok = sq.value().crc_ok; crc_avail = sq.value().radio_crc_available;
        }
        const auto decision = classify_candidate(mode_, manchester_enabled_, len_, true, rssi, lqi);
        if (decision != CaptureDecision::Accept) {
            counters_.noise_rejections++;
            if (decision == CaptureDecision::RejectVariantBShortTimeout) {
                counters_.variant_b_short_rejections++;
                PriosCaptureService::instance().record_noise_rejection(
                    manchester_enabled_, true);
            } else if (decision == CaptureDecision::RejectDiscoveryWeakSignal) {
                counters_.quality_rejections++;
                PriosCaptureService::instance().record_quality_rejection();
                PriosCaptureService::instance().record_noise_rejection(
                    manchester_enabled_, false);
            } else {
                PriosCaptureService::instance().record_noise_rejection(
                    manchester_enabled_, false);
            }
            if (should_emit_verbose_session_log()) {
                ESP_LOGD(TAG_PRIOS,
                         "PRIOS capture rejected as noise: mode=%s variant=%s bytes=%u rssi=%d lqi=%u",
                         mode_ == Mode::DiscoverySniffer ? "discovery" : "campaign",
                         manchester_enabled_ ? "manchester_on" : "manchester_off",
                         len_, rssi, lqi);
            }
            if (verbose_session_logs_remaining_ > 0) {
                verbose_session_logs_remaining_--;
            }
            radio.abort_and_restart_rx();
            reset();
            return result;
        }
        if (should_emit_verbose_session_log()) {
            ESP_LOGD(TAG_PRIOS,
                     "PRIOS R3 capture timeout: %u bytes rssi=%d prefix=%02X %02X",
                     len_, rssi,
                     len_ > 0 ? buf_[0] : 0u,
                     len_ > 1 ? buf_[1] : 0u);
        }
        result.record      = finalise(rssi, lqi, crc_ok, crc_avail, timestamp_ms);
        result.has_capture = true;
        counters_.captures_completed++;
        counters_.timeout_captures++;
        if (verbose_session_logs_remaining_ > 0) {
            verbose_session_logs_remaining_--;
        }
        radio.abort_and_restart_rx();
        reset();
        return result;
    }

    // Session open with no progress for too long (no bytes at all).
    if (session_active_ && len_ == 0 &&
        (now_ms - session_start_ms_) >= kIdleTimeoutMs) {
        counters_.empty_resets++;
        ESP_LOGD(TAG_PRIOS, "PRIOS R3 bringup session: no bytes in %u ms, resetting",
                 kIdleTimeoutMs);
        radio.abort_and_restart_rx();
        reset();
    }

    return result;
}

} // namespace wmbus_prios_rx
