// Host tests for wmbus_prios_rx pure-logic components.
//
// Covers:
//   PriosCaptureDriver:
//     - identity contracts (protocol_id, required_radio_profile, max_session_encoded_bytes)
//     - feed_byte NeedMoreData until budget
//     - feed_byte returns FrameComplete at budget
//     - feed_byte after FrameComplete stays FrameComplete
//     - finalize_frame before FrameComplete returns false
//     - finalize_frame fills ProtocolFrame correctly
//     - decode_telegram always returns false (no decoder yet)
//     - reset_session clears state
//     - reset_session allows fresh session
//
//   PriosCaptureService:
//     - empty snapshot is valid
//     - insert stores a record
//     - snapshot returns records in insertion order (oldest first)
//     - ring buffer evicts oldest on overflow
//     - total_inserted and total_evicted counts are correct
//     - clear resets the buffer

#include "host_test_stubs.hpp"
#include "protocol_driver/protocol_ids.hpp"
#include "protocol_driver/protocol_frame.hpp"
#include "protocol_driver/decoded_telegram.hpp"
#include "wmbus_prios_rx/prios_bringup_session.hpp"
#include "wmbus_prios_rx/prios_capture_driver.hpp"
#include "wmbus_prios_rx/prios_capture_service.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace protocol_driver;
using namespace wmbus_prios_rx;

namespace {

// ---- PriosCaptureDriver helpers ----

// Feed N bytes into driver; return final status.
DriverFeedStatus feed_n(PriosCaptureDriver& drv, uint16_t n, uint8_t fill = 0xAA) {
    DriverFeedResult res{};
    for (uint16_t i = 0; i < n; ++i) {
        res = drv.feed_byte(fill);
    }
    return res.status;
}

// ---- PriosCaptureDriver tests ----

void test_driver_identity_contracts() {
    PriosCaptureDriver drv;
    assert(drv.protocol_id()            == ProtocolId::WMbusPrios);
    assert(drv.required_radio_profile() == RadioProfileId::WMbusPriosR3);
    assert(drv.max_session_encoded_bytes() == PriosCaptureDriver::kMaxCaptureBytes);
    assert(drv.max_session_encoded_bytes() > 0);
    std::printf("  PASS: PriosCaptureDriver identity contracts\n");
}

void test_driver_feed_need_more_data_until_budget() {
    PriosCaptureDriver drv;
    const uint16_t budget = PriosCaptureDriver::kMaxCaptureBytes;

    // Feed budget - 1 bytes: must all be NeedMoreData.
    for (uint16_t i = 0; i < budget - 1; ++i) {
        const auto res = drv.feed_byte(0xAA);
        assert(res.status == DriverFeedStatus::NeedMoreData);
    }
    std::printf("  PASS: NeedMoreData until budget-1\n");
}

void test_driver_feed_frame_complete_at_budget() {
    PriosCaptureDriver drv;
    const auto status = feed_n(drv, PriosCaptureDriver::kMaxCaptureBytes);
    assert(status == DriverFeedStatus::FrameComplete);
    assert(drv.captured_length() == PriosCaptureDriver::kMaxCaptureBytes);
    std::printf("  PASS: FrameComplete at byte budget\n");
}

void test_driver_feed_after_complete_stays_complete() {
    PriosCaptureDriver drv;
    feed_n(drv, PriosCaptureDriver::kMaxCaptureBytes);
    const auto res = drv.feed_byte(0x55);
    assert(res.status == DriverFeedStatus::FrameComplete);
    std::printf("  PASS: feed_byte after FrameComplete stays FrameComplete\n");
}

void test_driver_finalize_before_complete_returns_false() {
    PriosCaptureDriver drv;
    drv.feed_byte(0xAA);
    ProtocolFrame frame{};
    assert(!drv.finalize_frame(frame));
    std::printf("  PASS: finalize_frame before FrameComplete returns false\n");
}

void test_driver_finalize_fills_frame() {
    PriosCaptureDriver drv;
    const uint16_t budget = PriosCaptureDriver::kMaxCaptureBytes;
    for (uint16_t i = 0; i < budget; ++i) {
        drv.feed_byte(static_cast<uint8_t>(i & 0xFF));
    }

    ProtocolFrame frame{};
    assert(drv.finalize_frame(frame));
    assert(frame.frame_complete);
    assert(frame.encoded_length == budget);
    assert(frame.decoded_length == 0); // no PHY decode in bring-up
    assert(frame.metadata.protocol      == ProtocolId::WMbusPrios);
    assert(frame.metadata.radio_profile == RadioProfileId::WMbusPriosR3);
    assert(frame.metadata.end_reason    == FrameCaptureEndReason::Complete);

    for (uint16_t i = 0; i < budget; ++i) {
        assert(frame.encoded_bytes[i] == static_cast<uint8_t>(i & 0xFF));
    }
    std::printf("  PASS: finalize_frame fills ProtocolFrame correctly\n");
}

void test_driver_decode_telegram_always_false() {
    PriosCaptureDriver drv;
    feed_n(drv, PriosCaptureDriver::kMaxCaptureBytes);
    ProtocolFrame frame{};
    drv.finalize_frame(frame);

    DecodedTelegram tg{};
    assert(!drv.decode_telegram(frame, tg));
    std::printf("  PASS: decode_telegram always returns false (no decoder)\n");
}

void test_driver_reset_clears_state() {
    PriosCaptureDriver drv;
    feed_n(drv, PriosCaptureDriver::kMaxCaptureBytes);
    drv.reset_session();
    assert(drv.captured_length() == 0);
    ProtocolFrame frame{};
    assert(!drv.finalize_frame(frame));
    std::printf("  PASS: reset_session clears driver state\n");
}

void test_driver_reset_allows_fresh_session() {
    PriosCaptureDriver drv;
    feed_n(drv, PriosCaptureDriver::kMaxCaptureBytes);
    drv.reset_session();
    // Second session: feed budget-1, should still be NeedMoreData.
    const uint16_t budget = PriosCaptureDriver::kMaxCaptureBytes;
    for (uint16_t i = 0; i < budget - 1; ++i) {
        const auto res = drv.feed_byte(0xBB);
        assert(res.status == DriverFeedStatus::NeedMoreData);
    }
    const auto final_res = drv.feed_byte(0xBB);
    assert(final_res.status == DriverFeedStatus::FrameComplete);
    assert(drv.captured_length() == budget);
    std::printf("  PASS: reset allows fresh session\n");
}

// ---- PriosCaptureService tests ----

void test_service_empty_snapshot() {
    // Use a local buffer, not the singleton, to avoid cross-test pollution.
    // The service is a singleton so we clear it first.
    PriosCaptureService::instance().clear();
    const auto snap = PriosCaptureService::instance().snapshot();
    assert(snap.count          == 0);
    assert(snap.total_inserted == 0);
    assert(snap.total_evicted  == 0);
    std::printf("  PASS: empty snapshot is valid\n");
}

void test_service_stats_are_lightweight_and_correct() {
    PriosCaptureService::instance().clear();

    PriosCaptureRecord r1{};
    r1.sequence = 1;
    PriosCaptureService::instance().insert(r1);

    PriosCaptureRecord r2{};
    r2.sequence = 2;
    PriosCaptureService::instance().insert(r2);

    const auto stats = PriosCaptureService::instance().stats();
    assert(stats.count == 2);
    assert(stats.total_inserted == 2);
    assert(stats.total_evicted == 0);
    assert(stats.total_burst_starts == 0);
    assert(stats.total_noise_rejected == 0);
    assert(stats.total_quality_rejected == 0);
    assert(stats.variant_b_short_rejected == 0);
    assert(stats.total_similarity_rejected == 0);
    static_assert(sizeof(PriosCaptureStats) < sizeof(PriosCaptureSnapshot),
                  "Stats accessor must stay lighter than full snapshot");
    std::printf("  PASS: lightweight stats accessor reports capture counters\n");
}

void test_service_noise_rejection_stats_are_tracked() {
    PriosCaptureService::instance().clear();

    PriosCaptureService::instance().record_noise_rejection(true, true);
    PriosCaptureService::instance().record_noise_rejection(false, false);
    PriosCaptureService::instance().record_quality_rejection();
    PriosCaptureService::instance().record_burst_start();

    const auto stats = PriosCaptureService::instance().stats();
    assert(stats.total_burst_starts == 1);
    assert(stats.total_noise_rejected == 2);
    assert(stats.total_quality_rejected == 1);
    assert(stats.variant_b_short_rejected == 1);
    assert(stats.total_similarity_rejected == 0);
    std::printf("  PASS: noise rejection stats tracked separately from retained captures\n");
}

void test_service_capacity_is_64_records() {
    static_assert(PriosCaptureSnapshot::kMaxRecords == 64,
                  "PRIOS capture retention depth must remain 64 for offline analysis");
    assert(PriosCaptureSnapshot::kMaxRecords == 64);
    std::printf("  PASS: capture retention depth is 64 records\n");
}

void test_service_insert_and_retrieve() {
    PriosCaptureService::instance().clear();

    PriosCaptureRecord rec{};
    rec.sequence              = 1;
    rec.rssi_dbm              = -75;
    rec.total_bytes_captured  = 32;
    rec.captured_bytes[0]     = 0xAA;
    rec.captured_bytes[1]     = 0xBB;
    rec.manchester_enabled    = false;  // Variant A

    PriosCaptureService::instance().insert(rec);
    const auto snap = PriosCaptureService::instance().snapshot();

    assert(snap.count          == 1);
    assert(snap.total_inserted == 1);
    assert(snap.total_evicted  == 0);
    assert(snap.records[0].sequence             == 1);
    assert(snap.records[0].rssi_dbm             == -75);
    assert(snap.records[0].total_bytes_captured == 32);
    assert(snap.records[0].captured_bytes[0]    == 0xAA);
    assert(snap.records[0].captured_bytes[1]    == 0xBB);
    assert(snap.records[0].manchester_enabled   == false);
    std::printf("  PASS: insert and retrieve single record\n");
}

void test_service_preserves_full_bounded_capture() {
    PriosCaptureService::instance().clear();

    PriosCaptureRecord rec{};
    rec.sequence = 7;
    rec.total_bytes_captured = PriosCaptureRecord::kMaxCaptureBytes;
    for (size_t i = 0; i < PriosCaptureRecord::kMaxCaptureBytes; ++i) {
        rec.captured_bytes[i] = static_cast<uint8_t>(i);
    }

    PriosCaptureService::instance().insert(rec);
    const auto snap = PriosCaptureService::instance().snapshot();

    assert(snap.count == 1);
    assert(snap.records[0].total_bytes_captured == PriosCaptureRecord::kMaxCaptureBytes);
    for (size_t i = 0; i < PriosCaptureRecord::kMaxCaptureBytes; ++i) {
        assert(snap.records[0].captured_bytes[i] == static_cast<uint8_t>(i));
    }
    std::printf("  PASS: full bounded capture bytes are preserved\n");
}

void test_service_manchester_variant_stored() {
    // Verify that Variant A and Variant B records are distinguished correctly.
    PriosCaptureService::instance().clear();

    PriosCaptureRecord ra{};
    ra.sequence           = 10;
    ra.manchester_enabled = false;  // Variant A
    PriosCaptureService::instance().insert(ra);

    PriosCaptureRecord rb{};
    rb.sequence           = 11;
    rb.manchester_enabled = true;   // Variant B
    PriosCaptureService::instance().insert(rb);

    const auto snap = PriosCaptureService::instance().snapshot();
    assert(snap.count == 2);
    assert(snap.records[0].manchester_enabled == false);
    assert(snap.records[1].manchester_enabled == true);
    std::printf("  PASS: manchester_enabled stored and retrieved correctly per record\n");
}

void test_service_insertion_order() {
    PriosCaptureService::instance().clear();
    for (uint32_t i = 1; i <= 3; ++i) {
        PriosCaptureRecord r{};
        r.sequence = i;
        PriosCaptureService::instance().insert(r);
    }
    const auto snap = PriosCaptureService::instance().snapshot();
    assert(snap.count == 3);
    // Oldest first.
    assert(snap.records[0].sequence == 1);
    assert(snap.records[1].sequence == 2);
    assert(snap.records[2].sequence == 3);
    std::printf("  PASS: snapshot returns records in insertion order\n");
}

void test_service_ring_eviction() {
    PriosCaptureService::instance().clear();
    const size_t cap = PriosCaptureSnapshot::kMaxRecords;

    // Fill to capacity + 1 extra.
    for (uint32_t i = 1; i <= cap + 1; ++i) {
        PriosCaptureRecord r{};
        r.sequence = i;
        PriosCaptureService::instance().insert(r);
    }

    const auto snap = PriosCaptureService::instance().snapshot();
    assert(snap.count          == cap);
    assert(snap.total_inserted == cap + 1);
    assert(snap.total_evicted  == 1);
    // Oldest (seq=1) evicted; now oldest is seq=2.
    assert(snap.records[0].sequence == 2);
    assert(snap.records[cap - 1].sequence == cap + 1);
    std::printf("  PASS: ring buffer evicts oldest on overflow\n");
}

void test_service_preview_snapshot_is_bounded_and_recent() {
    PriosCaptureService::instance().clear();

    const size_t retained = PriosCaptureSnapshot::kMaxRecords;
    const size_t preview = PriosCapturePreviewSnapshot::kMaxRecords;
    for (uint32_t i = 1; i <= retained; ++i) {
        PriosCaptureRecord r{};
        r.sequence = i;
        r.total_bytes_captured = PriosCaptureRecord::kDisplayPrefixBytes + 4;
        for (size_t j = 0; j < PriosCaptureRecord::kMaxCaptureBytes; ++j) {
            r.captured_bytes[j] = static_cast<uint8_t>(i + j);
        }
        PriosCaptureService::instance().insert(r);
    }

    const auto snap = PriosCaptureService::instance().preview_snapshot();
    assert(snap.count == preview);
    assert(snap.total_inserted == retained);
    assert(snap.total_evicted == 0);
    assert(snap.records[0].sequence == retained - preview + 1);
    assert(snap.records[preview - 1].sequence == retained);
    assert(snap.records[0].preview_length == PriosCaptureRecord::kDisplayPrefixBytes);
    assert(snap.records[0].preview_bytes[0] ==
           static_cast<uint8_t>((retained - preview + 1) + 0));
    assert(snap.records[preview - 1].preview_bytes[0] ==
           static_cast<uint8_t>(retained + 0));
    static_assert(sizeof(PriosCapturePreviewSnapshot) < sizeof(PriosCaptureSnapshot),
                  "Live diagnostics preview must stay lighter than the full export snapshot");
    std::printf("  PASS: preview snapshot stays bounded to recent lightweight rows\n");
}

void test_service_allocated_snapshot_preserves_capacity_and_order() {
    PriosCaptureService::instance().clear();
    const size_t cap = PriosCaptureSnapshot::kMaxRecords;

    for (uint32_t i = 1; i <= cap; ++i) {
        PriosCaptureRecord r{};
        r.sequence = i;
        r.total_bytes_captured = PriosCaptureRecord::kMaxCaptureBytes;
        r.captured_bytes[0] = static_cast<uint8_t>(i);
        PriosCaptureService::instance().insert(r);
    }

    const auto snap = PriosCaptureService::instance().snapshot_allocated();
    assert(snap);
    assert(snap->count == cap);
    assert(snap->total_inserted == cap);
    assert(snap->total_evicted == 0);
    assert(snap->records[0].sequence == 1);
    assert(snap->records[cap - 1].sequence == cap);
    assert(snap->records[cap - 1].captured_bytes[0] == static_cast<uint8_t>(cap));
    std::printf("  PASS: allocated snapshot preserves full retained buffer\n");
}

void test_service_snapshot_count_reaches_full_capacity() {
    PriosCaptureService::instance().clear();
    const size_t cap = PriosCaptureSnapshot::kMaxRecords;

    for (uint32_t i = 1; i <= cap; ++i) {
        PriosCaptureRecord r{};
        r.sequence = i;
        PriosCaptureService::instance().insert(r);
    }

    const auto snap = PriosCaptureService::instance().snapshot();
    assert(snap.count == cap);
    assert(snap.total_inserted == cap);
    assert(snap.total_evicted == 0);
    assert(snap.records[0].sequence == 1);
    assert(snap.records[cap - 1].sequence == cap);
    std::printf("  PASS: snapshot retains the full configured capacity\n");
}

void test_service_clear_resets() {
    PriosCaptureService::instance().clear();
    PriosCaptureRecord r{};
    r.sequence = 99;
    PriosCaptureService::instance().insert(r);
    PriosCaptureService::instance().record_noise_rejection(true, true);
    PriosCaptureService::instance().clear();
    const auto snap = PriosCaptureService::instance().snapshot();
    const auto stats = PriosCaptureService::instance().stats();
    assert(snap.count          == 0);
    assert(snap.total_inserted == 0);
    assert(snap.total_evicted  == 0);
    assert(stats.total_noise_rejected == 0);
    assert(stats.total_burst_starts == 0);
    assert(stats.total_quality_rejected == 0);
    assert(stats.variant_b_short_rejected == 0);
    assert(stats.total_similarity_rejected == 0);
    std::printf("  PASS: clear() resets ring buffer\n");
}

void test_variant_b_similarity_gate_requires_a_repeat_before_retention() {
    PriosCaptureService::instance().clear();

    PriosCaptureRecord rec{};
    rec.sequence = 1;
    rec.manchester_enabled = true;
    rec.total_bytes_captured = PriosCaptureRecord::kMaxCaptureBytes;
    const uint8_t prefix[] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    std::memcpy(rec.captured_bytes, prefix, sizeof(prefix));

    const auto first = PriosCaptureService::instance().insert_with_quality_gate(rec);
    const auto after_first = PriosCaptureService::instance().stats();
    assert(first == PriosCaptureInsertDecision::RejectedVariantBSimilarity);
    assert(after_first.count == 0);
    assert(after_first.total_inserted == 0);
    assert(after_first.total_similarity_rejected == 1);

    rec.sequence = 2;
    const auto second = PriosCaptureService::instance().insert_with_quality_gate(rec);
    const auto snap = PriosCaptureService::instance().snapshot();
    const auto after_second = PriosCaptureService::instance().stats();
    assert(second == PriosCaptureInsertDecision::Inserted);
    assert(snap.count == 1);
    assert(after_second.total_inserted == 1);
    assert(after_second.total_similarity_rejected == 1);
    std::printf("  PASS: Variant B retention requires a repeated prefix\n");
}

void test_variant_a_bypasses_similarity_gate() {
    PriosCaptureService::instance().clear();

    PriosCaptureRecord rec{};
    rec.sequence = 1;
    rec.manchester_enabled = false;
    rec.total_bytes_captured = 16;
    rec.captured_bytes[0] = 0xAA;
    rec.captured_bytes[1] = 0xBB;

    const auto decision = PriosCaptureService::instance().insert_with_quality_gate(rec);
    const auto snap = PriosCaptureService::instance().snapshot();
    const auto stats = PriosCaptureService::instance().stats();
    assert(decision == PriosCaptureInsertDecision::Inserted);
    assert(snap.count == 1);
    assert(stats.total_inserted == 1);
    assert(stats.total_similarity_rejected == 0);
    std::printf("  PASS: Variant A remains unchanged by Variant B similarity gating\n");
}

void test_variant_b_short_timeout_is_rejected_as_noise() {
    const auto decision =
        PriosBringUpSession::classify_candidate(
            PriosBringUpSession::Mode::SyncCampaign,
            true,
            PriosBringUpSession::kVariantBMinTimeoutCaptureBytes - 1,
            true,
            -70,
            64);
    assert(decision == PriosBringUpSession::CaptureDecision::RejectVariantBShortTimeout);
    std::printf("  PASS: Variant B short timeout capture is rejected as noise\n");
}

void test_variant_a_and_longer_variant_b_captures_are_preserved() {
    assert(PriosBringUpSession::classify_candidate(
               PriosBringUpSession::Mode::SyncCampaign, false, 8, true, -70, 64) ==
           PriosBringUpSession::CaptureDecision::Accept);
    assert(PriosBringUpSession::classify_candidate(
               PriosBringUpSession::Mode::SyncCampaign,
               true,
               PriosBringUpSession::kVariantBMinTimeoutCaptureBytes,
               true,
               -70,
               64) == PriosBringUpSession::CaptureDecision::Accept);
    assert(PriosBringUpSession::classify_candidate(
               PriosBringUpSession::Mode::SyncCampaign, true, 8, false, -70, 64) ==
           PriosBringUpSession::CaptureDecision::Accept);
    std::printf("  PASS: Variant A and longer Variant B captures remain accepted\n");
}

void test_discovery_mode_uses_irq_or_fifo_instead_of_receiving_state() {
    radio_cc1101::RadioOwnerEventSet no_events{};
    wmbus_tmode_rx::SessionRadioStatus status{};
    status.receiving = true;
    status.fifo_bytes = 0;
    assert(!PriosBringUpSession::should_start_capture(
        PriosBringUpSession::Mode::DiscoverySniffer, no_events, status));

    auto irq_events = radio_cc1101::make_fallback_poll_event();
    assert(!PriosBringUpSession::should_start_capture(
        PriosBringUpSession::Mode::DiscoverySniffer, irq_events, status));

    irq_events = radio_cc1101::make_owner_events_from_irq(
        {radio_cc1101::GdoIrqSnapshot::bit_for(radio_cc1101::GdoPin::Gdo2), 0, 1});
    assert(PriosBringUpSession::should_start_capture(
        PriosBringUpSession::Mode::DiscoverySniffer, irq_events, status));

    status.fifo_bytes = 4;
    assert(PriosBringUpSession::should_start_capture(
        PriosBringUpSession::Mode::DiscoverySniffer, no_events, status));
    std::printf("  PASS: discovery mode starts on burst activity, not just RX state\n");
}

// ---- insert_with_dedup_gate tests -------------------------------------------

// Build a record with a known device fingerprint at bytes 9-14.
PriosCaptureRecord make_record_with_fp(const uint8_t fp_bytes[PriosDeviceFingerprint::kLength],
                                        uint8_t payload_variant = 0) {
    PriosCaptureRecord rec{};
    rec.total_bytes_captured = 20;
    for (uint8_t i = 0; i < 20; ++i) {
        rec.captured_bytes[i] = static_cast<uint8_t>(i + payload_variant);
    }
    // Overwrite fingerprint bytes at offset 9.
    for (uint8_t i = 0; i < PriosDeviceFingerprint::kLength; ++i) {
        rec.captured_bytes[PriosDeviceFingerprint::kOffset + i] = fp_bytes[i];
    }
    return rec;
}

void test_dedup_gate_inserts_first_record_for_device() {
    PriosCaptureService::instance().clear();
    const uint8_t fp[] = {0xB2, 0xB1, 0xC6, 0x9D, 0xA2, 0xCE};
    const auto rec = make_record_with_fp(fp, 0);
    const auto d = PriosCaptureService::instance().insert_with_dedup_gate(rec);
    assert(d == PriosCaptureInsertDecision::Inserted);
    assert(PriosCaptureService::instance().stats().total_inserted == 1);
    std::printf("  PASS: dedup gate inserts first record for a device\n");
}

void test_dedup_gate_rejects_exact_duplicate() {
    PriosCaptureService::instance().clear();
    const uint8_t fp[] = {0xB2, 0xB1, 0xC6, 0x9D, 0xA2, 0xCE};
    const auto rec = make_record_with_fp(fp, 0);

    const auto first  = PriosCaptureService::instance().insert_with_dedup_gate(rec);
    const auto second = PriosCaptureService::instance().insert_with_dedup_gate(rec);  // identical

    assert(first  == PriosCaptureInsertDecision::Inserted);
    assert(second == PriosCaptureInsertDecision::RejectedDuplicate);
    assert(PriosCaptureService::instance().stats().total_inserted == 1);
    assert(PriosCaptureService::instance().stats().total_dedup_rejected == 1);
    std::printf("  PASS: dedup gate rejects exact payload duplicate\n");
}

void test_dedup_gate_accepts_different_payload_same_device() {
    PriosCaptureService::instance().clear();
    const uint8_t fp[] = {0xB2, 0xB1, 0xC6, 0x9D, 0xA2, 0xCE};
    const auto rec_a = make_record_with_fp(fp, 0);
    const auto rec_b = make_record_with_fp(fp, 10);  // different payload bytes

    const auto da = PriosCaptureService::instance().insert_with_dedup_gate(rec_a);
    const auto db = PriosCaptureService::instance().insert_with_dedup_gate(rec_b);

    assert(da == PriosCaptureInsertDecision::Inserted);
    assert(db == PriosCaptureInsertDecision::Inserted);
    assert(PriosCaptureService::instance().stats().total_inserted == 2);
    assert(PriosCaptureService::instance().stats().total_dedup_rejected == 0);
    std::printf("  PASS: dedup gate accepts different payload from same device\n");
}

void test_dedup_gate_enforces_per_device_quota() {
    PriosCaptureService::instance().clear();
    const uint8_t fp[] = {0xB2, 0xB1, 0xC6, 0x9D, 0xA2, 0xCE};

    // Insert kMaxRecordsPerDevice unique records for one device.
    for (size_t i = 0; i < PriosCaptureService::kMaxRecordsPerDevice; ++i) {
        const auto rec = make_record_with_fp(fp, static_cast<uint8_t>(i));
        const auto d = PriosCaptureService::instance().insert_with_dedup_gate(rec);
        assert(d == PriosCaptureInsertDecision::Inserted);
    }
    assert(PriosCaptureService::instance().stats().total_inserted ==
           PriosCaptureService::kMaxRecordsPerDevice);

    // One more unique payload for the same device should be rejected.
    const auto extra = make_record_with_fp(fp, 0xFF);
    const auto d = PriosCaptureService::instance().insert_with_dedup_gate(extra);
    assert(d == PriosCaptureInsertDecision::RejectedDeviceQuota);
    assert(PriosCaptureService::instance().stats().total_device_quota_rejected == 1);
    assert(PriosCaptureService::instance().stats().total_inserted ==
           PriosCaptureService::kMaxRecordsPerDevice);
    std::printf("  PASS: dedup gate rejects 6th unique record for same device\n");
}

void test_dedup_gate_different_devices_have_independent_quotas() {
    PriosCaptureService::instance().clear();
    const uint8_t fp_a[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t fp_b[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    // Fill quota for device A.
    for (size_t i = 0; i < PriosCaptureService::kMaxRecordsPerDevice; ++i) {
        const auto rec = make_record_with_fp(fp_a, static_cast<uint8_t>(i));
        assert(PriosCaptureService::instance().insert_with_dedup_gate(rec) ==
               PriosCaptureInsertDecision::Inserted);
    }
    // Device B should still be insertable regardless of device A's quota.
    const auto rec_b = make_record_with_fp(fp_b, 0);
    assert(PriosCaptureService::instance().insert_with_dedup_gate(rec_b) ==
           PriosCaptureInsertDecision::Inserted);
    assert(PriosCaptureService::instance().stats().total_inserted ==
           PriosCaptureService::kMaxRecordsPerDevice + 1);
    std::printf("  PASS: different devices have independent quotas\n");
}

void test_dedup_gate_short_capture_bypasses_fingerprint_check() {
    // Capture shorter than kOffset+kLength (< 15 bytes) — no fingerprint possible.
    // Must be inserted without dedup/quota checks.
    PriosCaptureService::instance().clear();
    PriosCaptureRecord short_rec{};
    short_rec.total_bytes_captured = 5;
    for (uint8_t i = 0; i < 5; ++i) { short_rec.captured_bytes[i] = i; }

    const auto d1 = PriosCaptureService::instance().insert_with_dedup_gate(short_rec);
    const auto d2 = PriosCaptureService::instance().insert_with_dedup_gate(short_rec);
    // Both should be inserted (no fingerprint → no dedup).
    assert(d1 == PriosCaptureInsertDecision::Inserted);
    assert(d2 == PriosCaptureInsertDecision::Inserted);
    assert(PriosCaptureService::instance().stats().total_inserted == 2);
    std::printf("  PASS: short capture (no fingerprint) bypasses dedup gate\n");
}

void test_dedup_gate_stats_cleared_by_clear() {
    PriosCaptureService::instance().clear();
    const uint8_t fp[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    const auto rec = make_record_with_fp(fp, 0);
    (void)PriosCaptureService::instance().insert_with_dedup_gate(rec);
    (void)PriosCaptureService::instance().insert_with_dedup_gate(rec);  // duplicate
    PriosCaptureService::instance().clear();
    const auto stats = PriosCaptureService::instance().stats();
    assert(stats.total_dedup_rejected == 0);
    assert(stats.total_device_quota_rejected == 0);
    std::printf("  PASS: clear() resets dedup counters\n");
}

void test_new_device_limit_rejects_17th_fingerprint() {
    PriosCaptureService::instance().clear();

    // Insert kMaxTrackedDevices unique devices, each with one record.
    for (size_t i = 0; i < PriosCaptureService::kMaxTrackedDevices; ++i) {
        const uint8_t fp[6] = {
            static_cast<uint8_t>(i), 0x00, 0x00, 0x00, 0x00, 0x00
        };
        const auto rec = make_record_with_fp(fp, 0);
        const auto d = PriosCaptureService::instance().insert_with_dedup_gate(rec);
        assert(d == PriosCaptureInsertDecision::Inserted);
    }
    assert(PriosCaptureService::instance().stats().unique_devices_tracked ==
           PriosCaptureService::kMaxTrackedDevices);

    // 17th unique device must be rejected.
    const uint8_t fp_new[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const auto rec_new = make_record_with_fp(fp_new, 0);
    const auto d = PriosCaptureService::instance().insert_with_dedup_gate(rec_new);
    assert(d == PriosCaptureInsertDecision::RejectedNewDeviceLimit);
    assert(PriosCaptureService::instance().stats().total_new_device_limit_rejected == 1);
    // Total inserted must not have grown for the rejected device.
    assert(PriosCaptureService::instance().stats().total_inserted ==
           PriosCaptureService::kMaxTrackedDevices);
    std::printf("  PASS: new device limit rejects 17th unique fingerprint\n");
}

void test_new_device_limit_allows_existing_device_extra_records() {
    // After the global table is full, existing devices may still insert
    // records up to their per-device quota (they're not "new").
    PriosCaptureService::instance().clear();

    for (size_t i = 0; i < PriosCaptureService::kMaxTrackedDevices; ++i) {
        const uint8_t fp[6] = {
            static_cast<uint8_t>(i), 0x00, 0x00, 0x00, 0x00, 0x00
        };
        const auto rec = make_record_with_fp(fp, 0);
        assert(PriosCaptureService::instance().insert_with_dedup_gate(rec) ==
               PriosCaptureInsertDecision::Inserted);
    }

    // Device 0 already in table — second unique payload should still be accepted.
    const uint8_t fp_known[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const auto rec_known = make_record_with_fp(fp_known, 0x42);  // different payload byte
    const auto d = PriosCaptureService::instance().insert_with_dedup_gate(rec_known);
    assert(d == PriosCaptureInsertDecision::Inserted);
    assert(PriosCaptureService::instance().stats().total_new_device_limit_rejected == 0);
    std::printf("  PASS: known device is accepted even when global table is full\n");
}

void test_new_device_limit_cleared_by_clear() {
    PriosCaptureService::instance().clear();

    // Fill table then trigger a rejection.
    for (size_t i = 0; i < PriosCaptureService::kMaxTrackedDevices; ++i) {
        const uint8_t fp[6] = {
            static_cast<uint8_t>(i), 0x01, 0x00, 0x00, 0x00, 0x00
        };
        const auto rec = make_record_with_fp(fp, 0);
        (void)PriosCaptureService::instance().insert_with_dedup_gate(rec);
    }
    const uint8_t fp_over[6] = {0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA};
    (void)PriosCaptureService::instance().insert_with_dedup_gate(make_record_with_fp(fp_over, 0));
    assert(PriosCaptureService::instance().stats().total_new_device_limit_rejected == 1);

    PriosCaptureService::instance().clear();
    const auto stats = PriosCaptureService::instance().stats();
    assert(stats.total_new_device_limit_rejected == 0);
    assert(stats.unique_devices_tracked == 0);
    std::printf("  PASS: clear() resets new_device_limit counter and unique_devices_tracked\n");
}

void test_sync_campaign_starts_counter_is_incremented() {
    PriosCaptureService::instance().clear();

    const auto before = PriosCaptureService::instance().stats();
    assert(before.total_sync_campaign_starts == 0);
    assert(before.total_burst_starts == 0);

    PriosCaptureService::instance().record_burst_start();
    PriosCaptureService::instance().record_sync_campaign_start();
    PriosCaptureService::instance().record_burst_start();
    // Second burst is discovery-only (no sync_campaign call).

    const auto after = PriosCaptureService::instance().stats();
    assert(after.total_burst_starts == 2);
    assert(after.total_sync_campaign_starts == 1);
    std::printf("  PASS: sync_campaign_starts tracks only sync-triggered sessions\n");
}

void test_sync_campaign_starts_cleared_by_clear() {
    PriosCaptureService::instance().clear();
    PriosCaptureService::instance().record_sync_campaign_start();
    PriosCaptureService::instance().clear();
    const auto stats = PriosCaptureService::instance().stats();
    assert(stats.total_sync_campaign_starts == 0);
    std::printf("  PASS: clear() resets sync_campaign_starts\n");
}

void test_discovery_mode_rejects_weak_signal_candidates() {
    const auto decision = PriosBringUpSession::classify_candidate(
        PriosBringUpSession::Mode::DiscoverySniffer, true, 32, false,
        PriosBringUpSession::kDiscoveryMinRssiDbm - 1,
        PriosBringUpSession::kDiscoveryMinLqi - 1);
    assert(decision == PriosBringUpSession::CaptureDecision::RejectDiscoveryWeakSignal);
    std::printf("  PASS: discovery mode rejects weak-signal candidates\n");
}

} // namespace

int main() {
    std::printf("=== test_prios_capture ===\n");

    test_driver_identity_contracts();
    test_driver_feed_need_more_data_until_budget();
    test_driver_feed_frame_complete_at_budget();
    test_driver_feed_after_complete_stays_complete();
    test_driver_finalize_before_complete_returns_false();
    test_driver_finalize_fills_frame();
    test_driver_decode_telegram_always_false();
    test_driver_reset_clears_state();
    test_driver_reset_allows_fresh_session();

    test_service_empty_snapshot();
    test_service_stats_are_lightweight_and_correct();
    test_service_noise_rejection_stats_are_tracked();
    test_service_capacity_is_64_records();
    test_service_insert_and_retrieve();
    test_service_preserves_full_bounded_capture();
    test_service_manchester_variant_stored();
    test_service_insertion_order();
    test_service_snapshot_count_reaches_full_capacity();
    test_service_ring_eviction();
    test_service_preview_snapshot_is_bounded_and_recent();
    test_service_allocated_snapshot_preserves_capacity_and_order();
    test_service_clear_resets();
    test_variant_b_similarity_gate_requires_a_repeat_before_retention();
    test_variant_a_bypasses_similarity_gate();
    test_variant_b_short_timeout_is_rejected_as_noise();
    test_variant_a_and_longer_variant_b_captures_are_preserved();
    test_discovery_mode_uses_irq_or_fifo_instead_of_receiving_state();
    test_discovery_mode_rejects_weak_signal_candidates();
    test_dedup_gate_inserts_first_record_for_device();
    test_dedup_gate_rejects_exact_duplicate();
    test_dedup_gate_accepts_different_payload_same_device();
    test_dedup_gate_enforces_per_device_quota();
    test_dedup_gate_different_devices_have_independent_quotas();
    test_dedup_gate_short_capture_bypasses_fingerprint_check();
    test_dedup_gate_stats_cleared_by_clear();

    test_new_device_limit_rejects_17th_fingerprint();
    test_new_device_limit_allows_existing_device_extra_records();
    test_new_device_limit_cleared_by_clear();

    test_sync_campaign_starts_counter_is_incremented();
    test_sync_campaign_starts_cleared_by_clear();

    std::printf("All prios capture tests passed.\n");
    return 0;
}
