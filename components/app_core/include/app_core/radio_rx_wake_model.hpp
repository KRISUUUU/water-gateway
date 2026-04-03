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
        // Once a candidate is active, keep a bounded watchdog tick so stalled sessions can time
        // out even if the radio stops producing IRQ edges.
        return {session_watchdog_tick_ms, radio_cc1101::make_session_watchdog_tick_event(),
                RadioRxWaitSource::SessionWatchdogTimeout};
    }

    if (irq_plumbing_enabled) {
        // Idle operation is IRQ-first. The timeout is a rare liveness wake that doubles as a
        // bounded fallback RX check if an owner-task notification was missed.
        // This intentionally replaces the old fixed heartbeat model rather than recreating it.
        return {kRadioIdleLivenessTimeoutMs, radio_cc1101::make_fallback_poll_event(),
                RadioRxWaitSource::IdleLivenessTimeout};
    }

    // If IRQ plumbing is unavailable, keep the same owner-task RX path alive with an explicit,
    // bounded poll fallback rather than reintroducing a fast idle heartbeat.
    return {kRadioIdleFallbackPollMs, radio_cc1101::make_fallback_poll_event(),
            RadioRxWaitSource::FallbackPollTimeout};
}

} // namespace app_core
