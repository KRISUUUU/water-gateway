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
    rec.manchester_enabled    = manchester_enabled_;  // record which variant was active

    rec.prefix_length = static_cast<uint8_t>(
        std::min(static_cast<size_t>(len_),
                 PriosCaptureRecord::kMaxPrefixBytes));
    std::memcpy(rec.prefix, buf_, rec.prefix_length);

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

    if (!session_active_ && status.fifo_bytes == 0 && !status.receiving) {
        return result; // Nothing to capture yet.
    }

    // Begin a new session on first byte arrival.
    if (!session_active_ && (status.fifo_bytes > 0 || status.receiving)) {
        session_active_   = true;
        session_start_ms_ = now_ms;
        last_byte_ms_     = now_ms;
        ESP_LOGI(TAG_PRIOS,
                 "PRIOS R3 capture session start: variant=%s fallback_wake=%d",
                 manchester_enabled_ ? "manchester_on" : "manchester_off",
                 static_cast<int>(result.is_fallback_wake));
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
                // Log first 2 bytes of each session at INFO level.
                if (len_ == 0 && copy >= 1) {
                    if (copy >= 2) {
                        ESP_LOGI(TAG_PRIOS, "PRIOS R3 first bytes: %02X %02X (rssi pending)",
                                 tmp[0], tmp[1]);
                    } else {
                        ESP_LOGI(TAG_PRIOS, "PRIOS R3 first byte: %02X", tmp[0]);
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
        ESP_LOGW(TAG_PRIOS, "PRIOS R3 FIFO overflow after %u bytes", len_);
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
        ESP_LOGI(TAG_PRIOS,
                 "PRIOS R3 capture complete: %u bytes, rssi=%d, lqi=%u, "
                 "prefix=%02X %02X %02X %02X",
                 len_, rssi, lqi,
                 len_ > 0 ? buf_[0] : 0u,
                 len_ > 1 ? buf_[1] : 0u,
                 len_ > 2 ? buf_[2] : 0u,
                 len_ > 3 ? buf_[3] : 0u);
        result.record      = finalise(rssi, lqi, crc_ok, crc_avail, timestamp_ms);
        result.has_capture = true;
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
        ESP_LOGI(TAG_PRIOS,
                 "PRIOS R3 capture timeout: %u bytes, rssi=%d, "
                 "prefix=%02X %02X",
                 len_, rssi,
                 len_ > 0 ? buf_[0] : 0u,
                 len_ > 1 ? buf_[1] : 0u);
        result.record      = finalise(rssi, lqi, crc_ok, crc_avail, timestamp_ms);
        result.has_capture = true;
        radio.abort_and_restart_rx();
        reset();
        return result;
    }

    // Session open with no progress for too long (no bytes at all).
    if (session_active_ && len_ == 0 &&
        (now_ms - session_start_ms_) >= kIdleTimeoutMs) {
        ESP_LOGD(TAG_PRIOS, "PRIOS R3 bringup session: no bytes in %u ms, resetting",
                 kIdleTimeoutMs);
        radio.abort_and_restart_rx();
        reset();
    }

    return result;
}

} // namespace wmbus_prios_rx
