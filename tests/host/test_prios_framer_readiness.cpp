// test_prios_framer_readiness.cpp
//
// This file is NOT a framer test.
//
// It is a compile-time and runtime checklist that documents exactly which
// protocol facts must be confirmed before a real PRIOS framer can be written.
// Each "BLOCKED" assertion will remain failing until the matching fact is
// resolved from hardware captures.
//
// When you have verified a fact from hardware evidence:
//   1. Update the named constant in this file from kUnknown to the verified
//      value.
//   2. Update the corresponding BLOCKED assertion to CONFIRMED.
//   3. Commit the raw fixture data alongside the change.
//
// Do NOT guess values. Every constant below that is kUnknown reflects a real
// gap. Writing a framer before all REQUIRED facts are CONFIRMED produces a
// framer that silently drops real frames or accepts noise.
//
// Validation: ctest --test-dir tests/host/build -R test_prios_framer_readiness

#include "radio_cc1101/cc1101_profile_prios_r3.hpp"
#include "wmbus_prios_rx/prios_capture_driver.hpp"
#include "wmbus_prios_rx/prios_fixture.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>

// ---------------------------------------------------------------------------
// Protocol-fact registry
//
// Each entry documents one fact required by the framer.
// Status: UNKNOWN | CONFIRMED
// ---------------------------------------------------------------------------

// SYNC word — the 2-byte CC1101 sync word the hardware transmits before each
// PRIOS frame.  Required to: set SYNC1/SYNC0 registers, filter noise in the
// framer, and know where the frame starts relative to the CC1101 FIFO.
//
// Current value in cc1101_profile_prios_r3.hpp: 0x54/0x3D
// This is the T-mode T1/T2 sync word. The PRIOS hardware almost certainly uses
// a different value. It must be read from real hardware captures.
static constexpr bool kSyncWordKnown = false;           // BLOCKED

// Frame-length field — byte offset and width of the length field inside the
// PRIOS frame.  Required to: know when a frame is complete (instead of relying
// on a fixed byte budget).  Without this the driver can only use a fixed-size
// capture window.
static constexpr bool kFrameLengthFieldKnown = false;   // BLOCKED

// CRC scheme — algorithm (CRC-16/CCITT, CRC-16/KERMIT, proprietary, …),
// polynomial, byte range, and position of the checksum within the frame.
// Required to: validate received frames and reject noise/corruption.
static constexpr bool kCrcSchemeKnown = false;          // BLOCKED

// Modulation — confirmed 2-FSK vs Manchester-encoded 2-FSK.
// The CC1101 MANCHESTER_EN bit must match the hardware or every byte is wrong.
// Current assumption: Manchester disabled (T-mode default). Unconfirmed.
static constexpr bool kModulationKnown = false;         // BLOCKED

// Baud rate — PRIOS R3 nominal symbol rate.
// Current assumption: 32.768 kbaud (same as T-mode). Unconfirmed.
static constexpr bool kBaudRateKnown = false;           // BLOCKED

// ---------------------------------------------------------------------------
// Framer-readiness gate
//
// ALL of the following must be true before a framer is safe to implement.
// Fields marked REQUIRED cannot be waived; HELPFUL improves quality but the
// framer can operate without them.
// ---------------------------------------------------------------------------

struct FramerFact {
    const char* name;
    bool        known;
    bool        required;   // true = framer is blocked without this
};

static constexpr FramerFact kFramerFacts[] = {
    { "sync_word",            kSyncWordKnown,          true  },
    { "frame_length_field",   kFrameLengthFieldKnown,  true  },
    { "crc_scheme",           kCrcSchemeKnown,         true  },
    { "modulation",           kModulationKnown,        true  },
    { "baud_rate",            kBaudRateKnown,          false },  // HELPFUL
};

static constexpr size_t kFactCount = sizeof(kFramerFacts) / sizeof(kFramerFacts[0]);

// ---------------------------------------------------------------------------
// Capture-mode smoke tests
//
// These tests confirm the existing bounded capture infrastructure is intact.
// They do not test any framing logic (none exists).
// ---------------------------------------------------------------------------

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(name, expr) do { \
    if (expr) { printf("  PASS  %s\n", (name)); ++g_pass; } \
    else      { printf("  FAIL  %s\n", (name)); ++g_fail; } \
} while(0)

static void test_capture_driver_identity() {
    wmbus_prios_rx::PriosCaptureDriver drv;
    CHECK("capture_driver_protocol_id",
          drv.protocol_id() == protocol_driver::ProtocolId::WMbusPrios);
    CHECK("capture_driver_profile",
          drv.required_radio_profile() ==
              protocol_driver::RadioProfileId::WMbusPriosR3);
    CHECK("capture_driver_max_bytes",
          drv.max_session_encoded_bytes() ==
              wmbus_prios_rx::PriosCaptureDriver::kMaxCaptureBytes);
}

static void test_capture_driver_feed_completes_at_budget() {
    wmbus_prios_rx::PriosCaptureDriver drv;
    drv.reset_session();

    constexpr uint16_t kBudget = wmbus_prios_rx::PriosCaptureDriver::kMaxCaptureBytes;
    protocol_driver::DriverFeedResult last{};

    for (uint16_t i = 0; i < kBudget; ++i) {
        last = drv.feed_byte(static_cast<uint8_t>(i & 0xFF));
        if (i < kBudget - 1) {
            CHECK("need_more_data_before_budget",
                  last.status == protocol_driver::DriverFeedStatus::NeedMoreData);
        }
    }
    CHECK("frame_complete_at_budget",
          last.status == protocol_driver::DriverFeedStatus::FrameComplete);
}

static void test_capture_driver_no_decode() {
    wmbus_prios_rx::PriosCaptureDriver drv;
    drv.reset_session();
    for (uint16_t i = 0; i < wmbus_prios_rx::PriosCaptureDriver::kMaxCaptureBytes; ++i) {
        drv.feed_byte(0xAA);
    }
    protocol_driver::ProtocolFrame frame{};
    drv.finalize_frame(frame);

    protocol_driver::DecodedTelegram tg{};
    const bool decoded = drv.decode_telegram(frame, tg);
    CHECK("no_decode_until_framer_implemented", decoded == false);
}

static void test_fixture_frame_capacity() {
    CHECK("fixture_frame_max_bytes_bounded",
          wmbus_prios_rx::PriosFixtureFrame::kMaxBytes == 64);
    CHECK("fixture_suite_max_frames_bounded",
          wmbus_prios_rx::PriosFixtureSuite::kMaxFrames == 32);
}

// ---------------------------------------------------------------------------
// Framer-readiness report
// ---------------------------------------------------------------------------

static bool report_readiness() {
    printf("\n=== PRIOS Framer Readiness ===\n");
    bool all_required_known = true;
    bool any_unknown = false;

    for (size_t i = 0; i < kFactCount; ++i) {
        const auto& f = kFramerFacts[i];
        const char* status   = f.known    ? "CONFIRMED" : "UNKNOWN  ";
        const char* priority = f.required ? "REQUIRED " : "helpful  ";
        printf("  [%s] [%s] %s\n", status, priority, f.name);
        if (!f.known) {
            any_unknown = true;
            if (f.required) {
                all_required_known = false;
            }
        }
    }

    if (all_required_known) {
        printf("\n  READY — all required facts confirmed. Framer can be written.\n");
    } else {
        printf("\n  BLOCKED — resolve the UNKNOWN/REQUIRED facts above before\n");
        printf("  implementing a framer.  See docs/PRIOS_BRINGUP.md.\n");
    }

    (void)any_unknown;
    return all_required_known;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    printf("=== test_prios_framer_readiness ===\n\n");

    printf("-- Capture-mode smoke tests --\n");
    test_capture_driver_identity();
    test_capture_driver_feed_completes_at_budget();
    test_capture_driver_no_decode();
    test_fixture_frame_capacity();

    const bool framer_ready = report_readiness();

    printf("\n-- Result --\n");
    printf("%d capture-mode checks passed, %d failed\n", g_pass, g_fail);

    if (!framer_ready) {
        // This is the expected state until hardware captures exist.
        // Print a clear message but do NOT fail the test suite — the gap is
        // documented, not a regression.
        printf("Framer status: BLOCKED (expected — no hardware captures yet)\n");
    } else {
        printf("Framer status: READY (all required facts confirmed)\n");
    }

    // The test binary exits 0 as long as capture-mode smoke tests pass.
    // A non-zero exit would mark a regression in the capture infrastructure,
    // not the absence of a framer (which is the documented state).
    return g_fail > 0 ? 1 : 0;
}
