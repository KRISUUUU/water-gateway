#pragma once

#include "wmbus_prios_rx/prios_capture_service.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// PRIOS fixture types for host tests and offline analysis.
//
// A PriosFixtureFrame holds one bounded raw PRIOS capture. It is the unit of
// evidence used by:
//   - host tests that replay captures into the future decoder
//   - the PriosAnalyzer (prefix voting, length histogram)
//   - the fixture export endpoint (GET /api/diagnostics/prios/export)
//
// Design constraints:
//   - POD-friendly: no heap, no std::string, no virtual functions
//   - Embeddable as C++ aggregate literals in test source files
//   - Bounded: max kMaxBytes raw bytes per frame
//   - No invented semantics: raw bytes only, no field interpretation
//
// Embedding a fixture in a test file:
//
//   #include "wmbus_prios_rx/prios_fixture.hpp"
//   using namespace wmbus_prios_rx;
//
//   // Paste hex from GET /api/diagnostics/prios/export
//   static constexpr PriosFixtureFrame kCapture1 = {
//       .bytes  = {0xAA, 0xBB, 0xCC, ...},
//       .length = 18,
//       .rssi_dbm = -72,
//       .lqi = 90,
//       .label = "r3_meter_a_cap1",
//   };
//
//   static constexpr PriosFixtureFrame kSuite[] = { kCapture1, kCapture2 };

namespace wmbus_prios_rx {

struct PriosFixtureFrame {
    // Maximum raw bytes per fixture frame.
    // Matches PriosBringUpSession::kMaxCaptureBytes (64) to ensure all
    // live captures fit without truncation.
    static constexpr size_t kMaxBytes = 64;

    // Maximum length of the human-readable label (including NUL terminator).
    static constexpr size_t kMaxLabelLen = 48;

    // Raw captured bytes, exactly as received from the CC1101 FIFO.
    uint8_t  bytes[kMaxBytes]{};
    uint8_t  length = 0;

    // Signal quality from the capture session.
    int8_t   rssi_dbm = 0;
    uint8_t  lqi      = 0;
    bool     radio_crc_ok        = false;
    bool     radio_crc_available = false;

    // Monotonic or epoch timestamp at capture (0 if unavailable).
    int64_t  timestamp_ms = 0;

    // Optional label for human identification in test output.
    char     label[kMaxLabelLen]{};

    // Construct a fixture frame from a live PriosCaptureRecord.
    // Truncates to kMaxBytes if total_bytes_captured > kMaxBytes (the prefix
    // is already bounded at PriosCaptureRecord::kMaxPrefixBytes ≤ kMaxBytes).
    static PriosFixtureFrame from_record(const PriosCaptureRecord& rec,
                                          const char* lbl = nullptr) {
        PriosFixtureFrame f{};
        const uint8_t src_len = rec.prefix_length;
        const uint8_t copy = src_len < kMaxBytes
                                 ? src_len
                                 : static_cast<uint8_t>(kMaxBytes);
        std::memcpy(f.bytes, rec.prefix, copy);
        f.length              = copy;
        f.rssi_dbm            = rec.rssi_dbm;
        f.lqi                 = rec.lqi;
        f.radio_crc_ok        = rec.radio_crc_ok;
        f.radio_crc_available = rec.radio_crc_available;
        f.timestamp_ms        = rec.timestamp_ms;
        if (lbl) {
            std::strncpy(f.label, lbl, kMaxLabelLen - 1);
            f.label[kMaxLabelLen - 1] = '\0';
        }
        return f;
    }
};

// A named collection of fixture frames for one test scenario.
// Bounded: at most kMaxFrames frames.
struct PriosFixtureSuite {
    static constexpr size_t kMaxFrames = 32;

    // Human-readable name for this suite (e.g. "r3_meter_a_outdoor_2026-04").
    char     name[64]{};
    PriosFixtureFrame frames[kMaxFrames]{};
    size_t   count = 0;

    // Append a frame. Returns false if the suite is full.
    bool append(const PriosFixtureFrame& f) {
        if (count >= kMaxFrames) {
            return false;
        }
        frames[count++] = f;
        return true;
    }
};

} // namespace wmbus_prios_rx
