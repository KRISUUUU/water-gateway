#include "host_test_stubs.hpp"
#include "radio_cc1101/cc1101_irq.hpp"
#include "radio_cc1101/cc1101_owner_events.hpp"
#include "radio_cc1101/cc1101_profile_tmode.hpp"
#include "radio_cc1101/radio_cc1101.hpp"
#include <cassert>
#include <cstdio>

using namespace radio_cc1101;


static void test_tmode_profile_lookup() {
    const auto* pktctrl0 = find_tmode_register_config(registers::PKTCTRL0);
    const auto* iocfg0 = find_tmode_register_config(registers::IOCFG0);
    assert(pktctrl0 != nullptr);
    assert(iocfg0 != nullptr);
    assert(pktctrl0->value == 0x02);
    assert(iocfg0->value == 0x00);
    assert(tmode_config_contains(registers::SYNC1, 0x54));
    assert(!tmode_config_contains(registers::SYNC1, 0x00));
    printf("  PASS: T-mode profile lookup helpers\n");
}

static void test_irq_tracker_records_both_pins() {
    GdoIrqTracker tracker;
    tracker.record_isr_edge(GdoPin::Gdo0);
    tracker.record_isr_edge(GdoPin::Gdo2);
    tracker.record_isr_edge(GdoPin::Gdo2);

    const auto snapshot = tracker.snapshot();
    assert(snapshot.has_edge(GdoPin::Gdo0));
    assert(snapshot.has_edge(GdoPin::Gdo2));
    assert(snapshot.edge_count(GdoPin::Gdo0) == 1);
    assert(snapshot.edge_count(GdoPin::Gdo2) == 2);

    const auto cleared = tracker.take_and_clear();
    assert(cleared.has_edge(GdoPin::Gdo0));
    assert(cleared.has_edge(GdoPin::Gdo2));
    assert(cleared.edge_count(GdoPin::Gdo2) == 2);
    const auto after_clear = tracker.snapshot();
    assert(after_clear.pending_mask == 0);
    assert(after_clear.gdo0_edges == 0);
    assert(after_clear.gdo2_edges == 0);
    printf("  PASS: IRQ tracker snapshot and clear\n");
}

static void test_owner_event_mapping_and_merge() {
    GdoIrqTracker tracker;
    tracker.record_isr_edge(GdoPin::Gdo0);
    tracker.record_isr_edge(GdoPin::Gdo2);

    const auto irq_events = make_owner_events_from_irq(tracker.take_and_clear());
    assert(!irq_events.has(RadioOwnerEvent::SessionWatchdogTick));
    assert(!irq_events.has(RadioOwnerEvent::FallbackPoll));
    assert(irq_events.has(RadioOwnerEvent::Gdo0Edge));
    assert(irq_events.has(RadioOwnerEvent::Gdo2Edge));
    assert(irq_events.irq_snapshot.gdo0_edges == 1);
    assert(irq_events.irq_snapshot.gdo2_edges == 1);

    const auto merged = merge_owner_events(make_session_watchdog_tick_event(), irq_events);
    assert(merged.has(RadioOwnerEvent::SessionWatchdogTick));
    assert(merged.has(RadioOwnerEvent::Gdo0Edge));
    assert(merged.has(RadioOwnerEvent::Gdo2Edge));
    assert(merged.should_attempt_rx_work(true));
    assert(irq_events.should_attempt_rx_work(false));
    assert(!make_session_watchdog_tick_event().should_attempt_rx_work(false));
    assert(make_session_watchdog_tick_event().should_attempt_rx_work(true));
    assert(make_fallback_poll_event().should_attempt_rx_work(false));
    const auto merged_irq_and_fallback = merge_owner_events(make_fallback_poll_event(), irq_events);
    assert(merged_irq_and_fallback.has(RadioOwnerEvent::FallbackPoll));
    assert(merged_irq_and_fallback.has(RadioOwnerEvent::Gdo0Edge));
    assert(merged_irq_and_fallback.has(RadioOwnerEvent::Gdo2Edge));
    printf("  PASS: owner event mapping and merge\n");
}

static void test_owner_claim_state_is_singular() {
    RadioOwnerClaimState state;
    void* owner_a = reinterpret_cast<void*>(0xA0);
    void* owner_b = reinterpret_cast<void*>(0xB0);

    assert(state.claim(owner_a));
    assert(state.is_claimed());
    assert(state.owned_by(owner_a));
    assert(state.claim(owner_a));
    assert(!state.claim(owner_b));
    assert(!state.release(owner_b));
    assert(state.release(owner_a));
    assert(!state.is_claimed());
    printf("  PASS: owner claim state is singular\n");
}


static void test_owner_only_rx_helpers_require_claim_and_return_status() {
    auto& radio = RadioCc1101::instance();
    void* owner = reinterpret_cast<void*>(0xCAFE);
    const SpiPins pins{23, 19, 18, 5, -1, -1};

    const auto init = radio.initialize(pins);
    assert(init.is_ok() || init.error() == common::ErrorCode::AlreadyInitialized);
    assert(radio.claim_owner(owner).is_ok());
    const auto status = radio.owner_read_rx_status(owner);
    assert(status.is_ok());
    assert(status.value().receiving);
    assert(status.value().fifo_bytes == 2);

    uint8_t buffer[4]{};
    const auto read = radio.owner_read_fifo_bytes(owner, buffer, sizeof(buffer));
    assert(read.is_ok());
    assert(read.value() == 2);
    assert(buffer[0] == 0x44);
    assert(buffer[1] == 0x93);

    const auto quality = radio.owner_read_signal_quality(owner);
    assert(quality.is_ok());
    assert(quality.value().rssi_dbm == -65);
    assert(quality.value().lqi == 45);

    assert(radio.owner_switch_to_fixed_length_capture(owner, 16).is_ok());
    assert(radio.owner_restore_infinite_packet_mode(owner).is_ok());
    assert(radio.owner_abort_and_restart_rx(owner).is_ok());
    radio.release_owner(owner);
    printf("  PASS: owner-only RX helpers exposed for session engine\n");
}

static void test_prios_profile_apply_rearms_rx() {
    auto& radio = RadioCc1101::instance();
    void* owner = reinterpret_cast<void*>(0xD00D);
    const SpiPins pins{23, 19, 18, 5, -1, -1};

    const auto init = radio.initialize(pins);
    assert(init.is_ok() || init.error() == common::ErrorCode::AlreadyInitialized);
    assert(radio.claim_owner(owner).is_ok());
    const auto apply = radio.owner_apply_prios_r3_profile(owner, false);
    assert(apply.is_ok());
    assert(radio.state() == RadioState::Receiving);
    radio.release_owner(owner);
    printf("  PASS: PRIOS profile apply leaves radio RX-ready\n");
}

int main() {
    printf("=== test_radio_cc1101 ===\n");

    test_tmode_profile_lookup();
    test_irq_tracker_records_both_pins();
    test_owner_event_mapping_and_merge();
    test_owner_claim_state_is_singular();

    test_owner_only_rx_helpers_require_claim_and_return_status();
    test_prios_profile_apply_rearms_rx();
    printf("All radio_cc1101 tests passed.\n");
    return 0;
}
