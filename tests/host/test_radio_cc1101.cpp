#include "host_test_stubs.hpp"
#include "radio_cc1101/cc1101_irq.hpp"
#include "radio_cc1101/cc1101_owner_events.hpp"
#include "radio_cc1101/cc1101_profile_prios_r3.hpp"
#include "radio_cc1101/cc1101_profile_prios_r4.hpp"
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

static void test_prios_r4_profile_apply_rearms_rx() {
    auto& radio = RadioCc1101::instance();
    void* owner = reinterpret_cast<void*>(0xD00F);
    const SpiPins pins{23, 19, 18, 5, -1, -1};

    const auto init = radio.initialize(pins);
    assert(init.is_ok() || init.error() == common::ErrorCode::AlreadyInitialized);
    assert(radio.claim_owner(owner).is_ok());
    const auto apply = radio.owner_apply_prios_r4_profile(owner, false);
    assert(apply.is_ok());
    assert(radio.state() == RadioState::Receiving);
    radio.release_owner(owner);
    printf("  PASS: PRIOS R4 profile apply leaves radio RX-ready\n");
}

static void test_tmode_profile_apply_rearms_rx() {
    auto& radio = RadioCc1101::instance();
    void* owner = reinterpret_cast<void*>(0xD00C);
    const SpiPins pins{23, 19, 18, 5, -1, -1};

    const auto init = radio.initialize(pins);
    assert(init.is_ok() || init.error() == common::ErrorCode::AlreadyInitialized);
    assert(radio.claim_owner(owner).is_ok());
    const auto apply = radio.owner_apply_tmode_profile(owner);
    assert(apply.is_ok());
    assert(radio.state() == RadioState::Receiving);
    radio.release_owner(owner);
    printf("  PASS: T-mode profile apply leaves radio RX-ready\n");
}

static void test_prios_discovery_profile_apply_rearms_rx() {
    auto& radio = RadioCc1101::instance();
    void* owner = reinterpret_cast<void*>(0xD00E);
    const SpiPins pins{23, 19, 18, 5, -1, -1};

    const auto init = radio.initialize(pins);
    assert(init.is_ok() || init.error() == common::ErrorCode::AlreadyInitialized);
    assert(radio.claim_owner(owner).is_ok());
    const auto apply = radio.owner_apply_prios_r3_discovery_profile(owner, true);
    assert(apply.is_ok());
    assert(radio.state() == RadioState::Receiving);
    radio.release_owner(owner);
    printf("  PASS: PRIOS discovery profile apply leaves radio RX-ready\n");
}

static void test_prios_r4_discovery_profile_apply_rearms_rx() {
    auto& radio = RadioCc1101::instance();
    void* owner = reinterpret_cast<void*>(0xD010);
    const SpiPins pins{23, 19, 18, 5, -1, -1};

    const auto init = radio.initialize(pins);
    assert(init.is_ok() || init.error() == common::ErrorCode::AlreadyInitialized);
    assert(radio.claim_owner(owner).is_ok());
    const auto apply = radio.owner_apply_prios_r4_discovery_profile(owner, true);
    assert(apply.is_ok());
    assert(radio.state() == RadioState::Receiving);
    radio.release_owner(owner);
    printf("  PASS: PRIOS R4 discovery profile apply leaves radio RX-ready\n");
}

static void test_prios_r4_profile_uses_868_30_mhz() {
    size_t count = 0;
    const auto* profile = prios_r4_profile(false, count);
    assert(profile != nullptr);
    bool found_freq2 = false;
    bool found_freq1 = false;
    bool found_freq0 = false;
    bool found_sync1 = false;
    bool found_sync0 = false;
    for (size_t i = 0; i < count; ++i) {
        if (profile[i].addr == registers::FREQ2) {
            assert(profile[i].value == 0x21);
            found_freq2 = true;
        }
        if (profile[i].addr == registers::FREQ1) {
            assert(profile[i].value == 0x65);
            found_freq1 = true;
        }
        if (profile[i].addr == registers::FREQ0) {
            assert(profile[i].value == 0x6A);
            found_freq0 = true;
        }
        if (profile[i].addr == registers::SYNC1) {
            assert(profile[i].value == 0x1E);
            found_sync1 = true;
        }
        if (profile[i].addr == registers::SYNC0) {
            assert(profile[i].value == 0x9B);
            found_sync0 = true;
        }
    }
    assert(found_freq2 && found_freq1 && found_freq0 && found_sync1 && found_sync0);
    printf("  PASS: PRIOS R4 profile uses 868.30 MHz and sync 0x1E9B\n");
}

static void test_prios_variant_b_profile_is_stricter_than_variant_a() {
    size_t count_a = 0;
    size_t count_b = 0;
    const auto* variant_a = prios_r3_profile(false, count_a);
    const auto* variant_b = prios_r3_profile(true, count_b);
    assert(variant_a != nullptr);
    assert(variant_b != nullptr);
    assert(count_a == count_b);

    bool found_a = false;
    bool found_b = false;
    for (size_t i = 0; i < count_a; ++i) {
        if (variant_a[i].addr == registers::MDMCFG2) {
            assert(variant_a[i].value == 0x02);
            found_a = true;
        }
    }
    for (size_t i = 0; i < count_b; ++i) {
        if (variant_b[i].addr == registers::MDMCFG2) {
            assert(variant_b[i].value == 0x0A);
            found_b = true;
        }
    }
    assert(found_a);
    assert(found_b);
    printf("  PASS: PRIOS Variant B profile keeps Manchester ON with middle-ground sync gating\n");
}

static void test_prios_discovery_profile_disables_sync_word_trigger() {
    size_t count_a = 0;
    size_t count_b = 0;
    const auto* variant_a = prios_r3_discovery_profile(false, count_a);
    const auto* variant_b = prios_r3_discovery_profile(true, count_b);
    assert(variant_a != nullptr);
    assert(variant_b != nullptr);
    assert(count_a == count_b);

    bool found_b_mdmcfg2 = false;
    bool found_b_iocfg2 = false;
    for (size_t i = 0; i < count_b; ++i) {
        if (variant_b[i].addr == registers::MDMCFG2) {
            assert(variant_b[i].value == 0x0C);
            found_b_mdmcfg2 = true;
        }
        if (variant_b[i].addr == registers::IOCFG2) {
            assert(variant_b[i].value == 0x0E);
            found_b_iocfg2 = true;
        }
    }
    assert(found_b_mdmcfg2);
    assert(found_b_iocfg2);
    printf("  PASS: PRIOS discovery profile uses carrier-gated capture instead of sync-word IRQs\n");
}

int main() {
    printf("=== test_radio_cc1101 ===\n");

    test_tmode_profile_lookup();
    test_irq_tracker_records_both_pins();
    test_owner_event_mapping_and_merge();
    test_owner_claim_state_is_singular();

    test_owner_only_rx_helpers_require_claim_and_return_status();
    test_tmode_profile_apply_rearms_rx();
    test_prios_profile_apply_rearms_rx();
    test_prios_r4_profile_apply_rearms_rx();
    test_prios_discovery_profile_apply_rearms_rx();
    test_prios_r4_discovery_profile_apply_rearms_rx();
    test_prios_variant_b_profile_is_stricter_than_variant_a();
    test_prios_discovery_profile_disables_sync_word_trigger();
    test_prios_r4_profile_uses_868_30_mhz();
    printf("All radio_cc1101 tests passed.\n");
    return 0;
}
