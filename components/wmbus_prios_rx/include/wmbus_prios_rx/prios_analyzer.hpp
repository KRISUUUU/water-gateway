#pragma once

#include "wmbus_prios_rx/prios_fixture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

// PriosAnalyzer: offline fixture analysis utilities.
//
// This is pure host-testable logic — no hardware, no FreeRTOS, no ESP-IDF.
// It operates on PriosFixtureFrame arrays and answers:
//
//   - Which byte positions carry a stable value across captures?
//     (Candidate preamble/sync bytes.)
//   - What is the longest common prefix shared by ≥ threshold% of captures?
//   - How are the capture lengths distributed?
//   - Is there enough evidence to start writing a decoder?
//
// None of this interprets protocol semantics. The outputs describe statistical
// properties of the raw byte streams only. Interpretation is left to the human.
//
// Usage:
//
//   PriosAnalyzer::ByteVote vote[PriosFixtureFrame::kMaxBytes]{};
//   PriosAnalyzer::compute_byte_votes(suite.frames, suite.count, vote);
//
//   const size_t cpl = PriosAnalyzer::common_prefix_length(vote,
//                          min_length, 75); // ≥75% agreement
//
//   PriosAnalyzer::LengthHistogram hist{};
//   PriosAnalyzer::compute_length_histogram(suite.frames, suite.count, hist);
//
//   PriosAnalyzer::Readiness r = PriosAnalyzer::assess_readiness(
//       suite.count, cpl, hist, vote);
//   // r.ready_for_decoder == true ⟹ safe to start writing framer logic

namespace wmbus_prios_rx {

class PriosAnalyzer {
  public:
    // ---- ByteVote: per-position stability analysis -------------------------

    struct ByteVote {
        // How many frames included a byte at this position.
        uint8_t  present_count = 0;

        // The value that appeared most often at this position.
        uint8_t  dominant_value = 0;

        // How many frames had the dominant value at this position.
        uint8_t  dominant_count = 0;

        // Agreement: dominant_count / present_count * 100 (integer %).
        // 0 when present_count == 0.
        uint8_t  agreement_pct = 0;
    };

    // Compute per-byte-position vote statistics across [frames, frames+count).
    // `out_votes` must have at least PriosFixtureFrame::kMaxBytes elements.
    static void compute_byte_votes(const PriosFixtureFrame* frames,
                                    size_t                   count,
                                    ByteVote*                out_votes);

    // ---- Common prefix -------------------------------------------------------

    // Length of the longest prefix where every position has
    // agreement_pct >= min_agreement_pct AND present_count >= min_frames.
    // Starts at byte 0 and stops at the first byte that fails either threshold.
    static size_t common_prefix_length(const ByteVote* votes,
                                        size_t          num_positions,
                                        uint8_t         min_agreement_pct,
                                        uint8_t         min_frames = 1);

    // ---- Length histogram ----------------------------------------------------

    struct LengthHistogram {
        // Counts for each possible capture length [0, PriosFixtureFrame::kMaxBytes].
        uint8_t  counts[PriosFixtureFrame::kMaxBytes + 1]{};
        uint8_t  min_length = 0xFF;
        uint8_t  max_length = 0;
        uint8_t  modal_length = 0;  // most common length
        uint8_t  modal_count  = 0;  // how many frames had modal_length
        size_t   total_frames = 0;
    };

    static void compute_length_histogram(const PriosFixtureFrame* frames,
                                          size_t                   count,
                                          LengthHistogram&         out_hist);

    // ---- Decoder readiness assessment ----------------------------------------

    struct Readiness {
        // True when evidence thresholds are all met.
        bool   ready_for_decoder = false;

        // Individual criteria (all must be true for ready_for_decoder).
        bool   enough_captures     = false; // >= kMinCaptures frames in suite
        bool   stable_prefix       = false; // common prefix >= kMinPrefixBytes
        bool   consistent_lengths  = false; // >= kLengthConsistencyPct% frames same length
        bool   good_rssi           = false; // at least one frame has rssi > kMinRssiDbm

        // Diagnostic values used to fill the criteria above.
        size_t capture_count    = 0;
        size_t prefix_length    = 0;
        uint8_t length_consistency_pct = 0;
        int8_t  best_rssi_dbm   = -128;

        // Minimum prefix agreement % used for this assessment.
        uint8_t agreement_pct_used = 0;
    };

    // Readiness thresholds (tunable; conservative defaults).
    static constexpr size_t  kMinCaptures           = 5;
    static constexpr size_t  kMinPrefixBytes        = 4;  // min stable bytes to trust sync
    static constexpr uint8_t kPrefixAgreementPct    = 75; // ≥75% of captures must agree
    static constexpr uint8_t kLengthConsistencyPct  = 60; // ≥60% same length
    static constexpr int8_t  kMinRssiDbm            = -100; // accept anything with signal

    // Assess whether the fixture suite has enough evidence to start a decoder.
    static Readiness assess_readiness(const PriosFixtureFrame* frames,
                                       size_t                   count,
                                       const LengthHistogram&   hist,
                                       const ByteVote*          votes);

    // ---- Device fingerprint extraction ---------------------------------------

    // Extract the device fingerprint from a raw PRIOS capture buffer.
    //
    // Returns {.valid=true, .bytes={frame[9]..frame[14]}} when
    // len >= PriosDeviceFingerprint::kOffset + PriosDeviceFingerprint::kLength.
    // Returns {.valid=false} if the capture is too short to contain the field.
    //
    // This is a pure function — no state, no heap, host-testable.
    static PriosDeviceFingerprint extract_fingerprint(const uint8_t* data, size_t len);

    // ---- Text report (for stdout / test output) ------------------------------

    // Write a compact human-readable analysis report into `buf` (NUL-terminated).
    // At most `buf_size` bytes written (including NUL). Returns bytes written.
    // Includes a per-device fingerprint grouping section when frames are provided.
    static size_t format_report(char*                    buf,
                                 size_t                   buf_size,
                                 const PriosFixtureFrame* frames,
                                 size_t                   count,
                                 const ByteVote*          votes,
                                 const LengthHistogram&   hist,
                                 const Readiness&         readiness);
};

} // namespace wmbus_prios_rx
