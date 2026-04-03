#include "app_core/radio_rx_wake_model.hpp"

#include <cassert>
#include <cstdio>

using namespace app_core;

namespace {

void test_idle_wait_is_irq_first_when_plumbing_available() {
    const auto policy = make_radio_rx_wake_policy(true, false, 6U);
    assert(policy.wait_timeout_ms == kRadioIdleLivenessTimeoutMs);
    assert(policy.timeout_source == RadioRxWaitSource::IdleLivenessTimeout);
    assert(policy.timeout_events.has(radio_cc1101::RadioOwnerEvent::FallbackPoll));
    assert(!policy.timeout_events.has(radio_cc1101::RadioOwnerEvent::SessionWatchdogTick));
    assert(!policy.timeout_events.has_any_irq());
    std::printf("  PASS: idle wait is IRQ-first with rare fallback RX check\n");
}

void test_active_session_uses_watchdog_tick() {
    const auto policy = make_radio_rx_wake_policy(true, true, 6U);
    assert(policy.wait_timeout_ms == 6U);
    assert(policy.timeout_source == RadioRxWaitSource::SessionWatchdogTimeout);
    assert(policy.timeout_events.has(radio_cc1101::RadioOwnerEvent::SessionWatchdogTick));
    std::printf("  PASS: active session uses watchdog tick timeout\n");
}

void test_no_irq_plumbing_uses_bounded_poll_fallback() {
    const auto policy = make_radio_rx_wake_policy(false, false, 6U);
    assert(policy.wait_timeout_ms == kRadioIdleFallbackPollMs);
    assert(policy.timeout_source == RadioRxWaitSource::FallbackPollTimeout);
    assert(policy.timeout_events.has(radio_cc1101::RadioOwnerEvent::FallbackPoll));
    std::printf("  PASS: missing IRQ plumbing uses bounded fallback poll\n");
}

void test_idle_wait_timeout_is_not_active_session_watchdog() {
    assert(kRadioIdleLivenessTimeoutMs > 100U);
    const auto idle_policy = make_radio_rx_wake_policy(true, false, 6U);
    const auto active_policy = make_radio_rx_wake_policy(true, true, 6U);
    assert(idle_policy.wait_timeout_ms != active_policy.wait_timeout_ms);
    assert(idle_policy.timeout_source == RadioRxWaitSource::IdleLivenessTimeout);
    assert(active_policy.timeout_source == RadioRxWaitSource::SessionWatchdogTimeout);
    std::printf("  PASS: idle timeout is distinct from active-session watchdog\n");
}

} // namespace

int main() {
    std::printf("=== test_radio_rx_wake_model ===\n");
    test_idle_wait_is_irq_first_when_plumbing_available();
    test_active_session_uses_watchdog_tick();
    test_no_irq_plumbing_uses_bounded_poll_fallback();
    test_idle_wait_timeout_is_not_active_session_watchdog();
    std::printf("All radio RX wake model tests passed.\n");
    return 0;
}
