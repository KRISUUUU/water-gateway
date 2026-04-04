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

void test_service_clear_resets() {
    PriosCaptureService::instance().clear();
    PriosCaptureRecord r{};
    r.sequence = 99;
    PriosCaptureService::instance().insert(r);
    PriosCaptureService::instance().clear();
    const auto snap = PriosCaptureService::instance().snapshot();
    assert(snap.count          == 0);
    assert(snap.total_inserted == 0);
    assert(snap.total_evicted  == 0);
    std::printf("  PASS: clear() resets ring buffer\n");
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
    test_service_insert_and_retrieve();
    test_service_preserves_full_bounded_capture();
    test_service_manchester_variant_stored();
    test_service_insertion_order();
    test_service_ring_eviction();
    test_service_clear_resets();

    std::printf("All prios capture tests passed.\n");
    return 0;
}
