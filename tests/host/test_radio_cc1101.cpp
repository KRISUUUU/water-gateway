#include "host_test_stubs.hpp"
#include "radio_cc1101/cc1101_irq.hpp"
#include "radio_cc1101/cc1101_owner_events.hpp"
#include "radio_cc1101/cc1101_profile_tmode.hpp"
#include "radio_cc1101/radio_cc1101.hpp"
#include <cassert>
#include <cstdio>

using namespace radio_cc1101;

static void test_timeout_ends_burst_even_when_fifo_is_not_empty() {
    const auto action = raw_burst_timeout_action(20, 33, 20);
    assert(action == RawBurstTimeoutAction::EndBurst);
    printf("  PASS: timeout ends burst with continuous FIFO data\n");
}

static void test_timeout_without_bytes_returns_not_found() {
    const auto action = raw_burst_timeout_action(20, 0, 20);
    assert(action == RawBurstTimeoutAction::ReturnNotFound);
    printf("  PASS: timeout without bytes returns not found\n");
}

static void test_sub_timeout_continues_capture() {
    const auto action = raw_burst_timeout_action(19, 64, 20);
    assert(action == RawBurstTimeoutAction::Continue);
    printf("  PASS: sub-timeout capture continues\n");
}

static void test_tmode_profile_lookup() {
    const auto* pktctrl0 = find_tmode_register_config(registers::PKTCTRL0);
    assert(pktctrl0 != nullptr);
    assert(pktctrl0->value == 0x02);
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
    assert(!irq_events.has(RadioOwnerEvent::PollTick));
    assert(irq_events.has(RadioOwnerEvent::Gdo0Edge));
    assert(irq_events.has(RadioOwnerEvent::Gdo2Edge));
    assert(irq_events.irq_snapshot.gdo0_edges == 1);
    assert(irq_events.irq_snapshot.gdo2_edges == 1);

    const auto merged = merge_owner_events(make_poll_tick_event(), irq_events);
    assert(merged.has(RadioOwnerEvent::PollTick));
    assert(merged.has(RadioOwnerEvent::Gdo0Edge));
    assert(merged.has(RadioOwnerEvent::Gdo2Edge));
    assert(merged.should_attempt_rx_work());
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

static void test_rssi_conversion_helper_via_public_contract() {
    auto& radio = RadioCc1101::instance();
    const auto init = radio.initialize({23, 19, 18, 5, -1, -1});
    assert(init.is_ok());
    const auto frame = radio.read_frame();
    assert(frame.is_ok());
    assert(frame.value().rssi_dbm == -65);
    assert(frame.value().lqi == 45);
    printf("  PASS: host radio path remains stable after internal split\n");
}

static void test_owner_only_rx_helpers_require_claim_and_return_status() {
    auto& radio = RadioCc1101::instance();
    void* owner = reinterpret_cast<void*>(0xCAFE);

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

int main() {
    printf("=== test_radio_cc1101 ===\n");
    test_timeout_ends_burst_even_when_fifo_is_not_empty();
    test_timeout_without_bytes_returns_not_found();
    test_sub_timeout_continues_capture();
    test_tmode_profile_lookup();
    test_irq_tracker_records_both_pins();
    test_owner_event_mapping_and_merge();
    test_owner_claim_state_is_singular();
    test_rssi_conversion_helper_via_public_contract();
    test_owner_only_rx_helpers_require_claim_and_return_status();
    printf("All radio_cc1101 tests passed.\n");
    return 0;
}
