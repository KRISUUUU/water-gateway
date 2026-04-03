#pragma once

#include "radio_cc1101/cc1101_owner_events.hpp"

#include <cstdint>

namespace app_core {

enum class RadioRxWaitSource : uint8_t {
    IdleLivenessTimeout = 0,
    SessionWatchdogTimeout,
    FallbackPollTimeout,
    IrqNotification,
};

struct RadioRxWakePolicy {
    uint32_t wait_timeout_ms = 0;
    radio_cc1101::RadioOwnerEventSet timeout_events{};
    RadioRxWaitSource timeout_source = RadioRxWaitSource::IdleLivenessTimeout;
};

constexpr uint32_t kRadioIdleLivenessTimeoutMs = 1000U;
constexpr uint32_t kRadioIdleFallbackPollMs = 50U;

constexpr RadioRxWakePolicy make_radio_rx_wake_policy(bool irq_plumbing_enabled,
                                                      bool session_active,
                                                      uint32_t session_watchdog_tick_ms) {
    if (session_active) {
        return {session_watchdog_tick_ms, radio_cc1101::make_session_watchdog_tick_event(),
                RadioRxWaitSource::SessionWatchdogTimeout};
    }

    if (irq_plumbing_enabled) {
        // Keep idle operation IRQ-first, but use a rare fallback RX check for liveness and to
        // recover from a missed owner-task notification without restoring a fast poll heartbeat.
        return {kRadioIdleLivenessTimeoutMs, radio_cc1101::make_fallback_poll_event(),
                RadioRxWaitSource::IdleLivenessTimeout};
    }

    return {kRadioIdleFallbackPollMs, radio_cc1101::make_fallback_poll_event(),
            RadioRxWaitSource::FallbackPollTimeout};
}

} // namespace app_core
