// Host tests for PriosFixtureFrame, PriosFixtureSuite, and PriosAnalyzer.
//
// All tests are pure host-side (no hardware, no FreeRTOS).
// Run via: ctest --test-dir tests/host/build

#include "wmbus_prios_rx/prios_fixture.hpp"
#include "wmbus_prios_rx/prios_analyzer.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace wmbus_prios_rx;

// ---- helpers -----------------------------------------------------------------

static int g_pass = 0;
static int g_fail = 0;

#define PASS(name) do { printf("  PASS  %s\n", name); ++g_pass; } while(0)
#define FAIL(name, ...) do { printf("  FAIL  %s: ", name); printf(__VA_ARGS__); printf("\n"); ++g_fail; } while(0)
#define CHECK(name, expr) do { if (expr) { PASS(name); } else { FAIL(name, "expression false"); } } while(0)

// Build a PriosCaptureRecord with a simple ascending byte pattern.
static PriosCaptureRecord make_record(uint8_t len, int8_t rssi = -70,
                                      uint8_t lqi = 80,
                                      bool crc_ok = true,
                                      bool crc_avail = true,
                                      int64_t ts = 12345) {
    PriosCaptureRecord r{};
    r.rssi_dbm            = rssi;
    r.lqi                 = lqi;
    r.radio_crc_ok        = crc_ok;
    r.radio_crc_available = crc_avail;
    r.timestamp_ms        = ts;
    r.total_bytes_captured = len;
    const uint8_t copy = len < PriosCaptureRecord::kMaxCaptureBytes
                             ? len
                             : static_cast<uint8_t>(PriosCaptureRecord::kMaxCaptureBytes);
    for (uint8_t i = 0; i < copy; ++i) {
        r.captured_bytes[i] = i;
    }
    return r;
}

// ---- PriosFixtureFrame -------------------------------------------------------

static void test_from_record_copies_fields() {
    const PriosCaptureRecord rec = make_record(10, -72, 90, true, true, 9999);
    const PriosFixtureFrame f = PriosFixtureFrame::from_record(rec, "my_label");

    CHECK("from_record_length",     f.length == 10);
    CHECK("from_record_rssi",       f.rssi_dbm == -72);
    CHECK("from_record_lqi",        f.lqi == 90);
    CHECK("from_record_crc_ok",     f.radio_crc_ok == true);
    CHECK("from_record_crc_avail",  f.radio_crc_available == true);
    CHECK("from_record_timestamp",  f.timestamp_ms == 9999);
    CHECK("from_record_label",      std::strcmp(f.label, "my_label") == 0);
    // Bytes should match the ascending pattern.
    bool bytes_ok = true;
    for (uint8_t i = 0; i < 10; ++i) {
        if (f.bytes[i] != i) { bytes_ok = false; }
    }
    CHECK("from_record_bytes", bytes_ok);
}

static void test_from_record_no_label() {
    const PriosCaptureRecord rec = make_record(5);
    const PriosFixtureFrame f = PriosFixtureFrame::from_record(rec);
    CHECK("from_record_no_label_empty", f.label[0] == '\0');
}

static void test_from_record_truncates_to_max() {
    PriosCaptureRecord rec = make_record(64, -60, 70, false, false, 0);
    const PriosFixtureFrame f = PriosFixtureFrame::from_record(rec);
    CHECK("from_record_truncate_length", f.length == 64);
    bool bytes_ok = true;
    for (uint8_t i = 0; i < 64; ++i) {
        if (f.bytes[i] != i) { bytes_ok = false; }
    }
    CHECK("from_record_truncate_bytes", bytes_ok);
}

// ---- PriosFixtureSuite -------------------------------------------------------

static void test_suite_append_and_count() {
    PriosFixtureSuite suite{};
    std::strncpy(suite.name, "test_suite", sizeof(suite.name) - 1);

    for (size_t i = 0; i < 5; ++i) {
        PriosFixtureFrame f{};
        f.length = static_cast<uint8_t>(i + 1);
        const bool ok = suite.append(f);
        CHECK("suite_append_ok", ok);
    }
    CHECK("suite_count_5", suite.count == 5);
}

static void test_suite_capacity_limit() {
    PriosFixtureSuite suite{};
    for (size_t i = 0; i < PriosFixtureSuite::kMaxFrames; ++i) {
        PriosFixtureFrame f{};
        suite.append(f);
    }
    CHECK("suite_full_count", suite.count == PriosFixtureSuite::kMaxFrames);

    // One more must be rejected.
    PriosFixtureFrame extra{};
    const bool rejected = !suite.append(extra);
    CHECK("suite_overflow_rejected", rejected);
    CHECK("suite_count_unchanged",   suite.count == PriosFixtureSuite::kMaxFrames);
}

// ---- PriosAnalyzer::compute_byte_votes ---------------------------------------

// Build a set of frames that all agree on bytes 0..3, then diverge.
static PriosFixtureFrame make_frame(const uint8_t* bytes, uint8_t len,
                                    int8_t rssi = -70) {
    PriosFixtureFrame f{};
    f.length    = len;
    f.rssi_dbm  = rssi;
    for (uint8_t i = 0; i < len; ++i) {
        f.bytes[i] = bytes[i];
    }
    return f;
}

static void test_byte_votes_stable_prefix() {
    // All 5 frames share bytes 0-3 = {0xAA, 0xBB, 0xCC, 0xDD}, then differ.
    const uint8_t base[] = {0xAA, 0xBB, 0xCC, 0xDD, 0x00};
    PriosFixtureFrame frames[5];
    for (int i = 0; i < 5; ++i) {
        uint8_t buf[5];
        std::memcpy(buf, base, 5);
        buf[4] = static_cast<uint8_t>(i);  // differs per frame
        frames[i] = make_frame(buf, 5);
    }

    PriosAnalyzer::ByteVote votes[PriosFixtureFrame::kMaxBytes]{};
    PriosAnalyzer::compute_byte_votes(frames, 5, votes);

    CHECK("votes_pos0_dominant",   votes[0].dominant_value == 0xAA);
    CHECK("votes_pos0_agreement",  votes[0].agreement_pct  == 100);
    CHECK("votes_pos1_dominant",   votes[1].dominant_value == 0xBB);
    CHECK("votes_pos1_agreement",  votes[1].agreement_pct  == 100);
    CHECK("votes_pos4_present",    votes[4].present_count  == 5);
    // Position 4 has all different values — dominant_count == 1, pct ≤ 20%.
    CHECK("votes_pos4_agreement_low", votes[4].agreement_pct <= 20);
}

static void test_byte_votes_empty_input() {
    PriosAnalyzer::ByteVote votes[PriosFixtureFrame::kMaxBytes]{};
    PriosAnalyzer::compute_byte_votes(nullptr, 0, votes);
    // Should not crash; votes remain zeroed.
    CHECK("votes_empty_no_crash", votes[0].present_count == 0);
}

static void test_byte_votes_absent_position() {
    // Frame 0 has length 2, frame 1 has length 4. Position 3 only appears in frame 1.
    const uint8_t b0[] = {0x11, 0x22};
    const uint8_t b1[] = {0x11, 0x22, 0x33, 0x44};
    PriosFixtureFrame frames[2] = { make_frame(b0, 2), make_frame(b1, 4) };

    PriosAnalyzer::ByteVote votes[PriosFixtureFrame::kMaxBytes]{};
    PriosAnalyzer::compute_byte_votes(frames, 2, votes);

    CHECK("votes_pos3_present_count_1", votes[3].present_count == 1);
    CHECK("votes_pos3_dominant",        votes[3].dominant_value == 0x44);
    CHECK("votes_pos3_agreement_100",   votes[3].agreement_pct  == 100);
}

// ---- PriosAnalyzer::common_prefix_length ------------------------------------

static void test_common_prefix_length_happy() {
    // 4 positions all with 100% agreement, 1 with 50%.
    PriosAnalyzer::ByteVote votes[5]{};
    for (int i = 0; i < 4; ++i) {
        votes[i].present_count  = 4;
        votes[i].dominant_count = 4;
        votes[i].agreement_pct  = 100;
    }
    votes[4].present_count  = 4;
    votes[4].dominant_count = 2;
    votes[4].agreement_pct  = 50;

    // With 75% threshold: prefix ends at position 4.
    const size_t cpl = PriosAnalyzer::common_prefix_length(votes, 5, 75, 1);
    CHECK("cpl_stops_at_bad_byte", cpl == 4);
}

static void test_common_prefix_length_all_agree() {
    PriosAnalyzer::ByteVote votes[6]{};
    for (int i = 0; i < 6; ++i) {
        votes[i].present_count  = 6;
        votes[i].dominant_count = 6;
        votes[i].agreement_pct  = 100;
    }
    const size_t cpl = PriosAnalyzer::common_prefix_length(votes, 6, 75, 1);
    CHECK("cpl_all_agree_full_length", cpl == 6);
}

static void test_common_prefix_length_null_input() {
    const size_t cpl = PriosAnalyzer::common_prefix_length(nullptr, 10, 75, 1);
    CHECK("cpl_null_returns_zero", cpl == 0);
}

static void test_common_prefix_length_min_frames_filter() {
    // Position 0 has only 1 frame present but threshold requires 3.
    PriosAnalyzer::ByteVote votes[3]{};
    votes[0].present_count  = 1;
    votes[0].dominant_count = 1;
    votes[0].agreement_pct  = 100;

    const size_t cpl = PriosAnalyzer::common_prefix_length(votes, 3, 75, 3);
    CHECK("cpl_min_frames_stops_at_0", cpl == 0);
}

// ---- PriosAnalyzer::compute_length_histogram --------------------------------

static void test_length_histogram_basic() {
    // 3 frames of length 18, 2 of length 20.
    PriosFixtureFrame frames[5]{};
    for (int i = 0; i < 3; ++i) { frames[i].length = 18; }
    for (int i = 3; i < 5; ++i) { frames[i].length = 20; }

    PriosAnalyzer::LengthHistogram hist{};
    PriosAnalyzer::compute_length_histogram(frames, 5, hist);

    CHECK("hist_total",      hist.total_frames == 5);
    CHECK("hist_min",        hist.min_length   == 18);
    CHECK("hist_max",        hist.max_length   == 20);
    CHECK("hist_modal_len",  hist.modal_length == 18);
    CHECK("hist_modal_cnt",  hist.modal_count  == 3);
    CHECK("hist_count_18",   hist.counts[18]   == 3);
    CHECK("hist_count_20",   hist.counts[20]   == 2);
}

static void test_length_histogram_empty() {
    PriosAnalyzer::LengthHistogram hist{};
    PriosAnalyzer::compute_length_histogram(nullptr, 0, hist);
    CHECK("hist_empty_total",  hist.total_frames == 0);
    CHECK("hist_empty_modal",  hist.modal_count  == 0);
}

// ---- PriosAnalyzer::assess_readiness ----------------------------------------

static void build_identical_frames(PriosFixtureFrame* frames, size_t count,
                                   int8_t rssi = -70) {
    const uint8_t bytes[] = {0x54, 0x3D, 0x01, 0x02, 0x03, 0x04};
    for (size_t i = 0; i < count; ++i) {
        frames[i] = make_frame(bytes, 6, rssi);
    }
}

static void test_readiness_all_criteria_met() {
    constexpr size_t N = 6;
    PriosFixtureFrame frames[N]{};
    build_identical_frames(frames, N, -70);  // good RSSI

    PriosAnalyzer::ByteVote votes[PriosFixtureFrame::kMaxBytes]{};
    PriosAnalyzer::compute_byte_votes(frames, N, votes);

    PriosAnalyzer::LengthHistogram hist{};
    PriosAnalyzer::compute_length_histogram(frames, N, hist);

    const auto r = PriosAnalyzer::assess_readiness(frames, N, hist, votes);

    CHECK("readiness_enough_captures",   r.enough_captures);
    CHECK("readiness_stable_prefix",     r.stable_prefix);
    CHECK("readiness_consistent_lengths",r.consistent_lengths);
    CHECK("readiness_good_rssi",         r.good_rssi);
    CHECK("readiness_ready",             r.ready_for_decoder);
}

static void test_readiness_too_few_captures() {
    constexpr size_t N = 2;  // < kMinCaptures (5)
    PriosFixtureFrame frames[N]{};
    build_identical_frames(frames, N);

    PriosAnalyzer::ByteVote votes[PriosFixtureFrame::kMaxBytes]{};
    PriosAnalyzer::compute_byte_votes(frames, N, votes);
    PriosAnalyzer::LengthHistogram hist{};
    PriosAnalyzer::compute_length_histogram(frames, N, hist);

    const auto r = PriosAnalyzer::assess_readiness(frames, N, hist, votes);
    CHECK("readiness_not_enough_caps",  !r.enough_captures);
    CHECK("readiness_not_ready",        !r.ready_for_decoder);
}

static void test_readiness_bad_rssi() {
    constexpr size_t N = 6;
    PriosFixtureFrame frames[N]{};
    build_identical_frames(frames, N, -110);  // below kMinRssiDbm (-100)

    PriosAnalyzer::ByteVote votes[PriosFixtureFrame::kMaxBytes]{};
    PriosAnalyzer::compute_byte_votes(frames, N, votes);
    PriosAnalyzer::LengthHistogram hist{};
    PriosAnalyzer::compute_length_histogram(frames, N, hist);

    const auto r = PriosAnalyzer::assess_readiness(frames, N, hist, votes);
    CHECK("readiness_bad_rssi",  !r.good_rssi);
    CHECK("readiness_not_ready", !r.ready_for_decoder);
}

static void test_readiness_unstable_prefix() {
    // Frames share only 1 byte (below kMinPrefixBytes=4).
    constexpr size_t N = 6;
    PriosFixtureFrame frames[N]{};
    for (size_t i = 0; i < N; ++i) {
        uint8_t buf[5] = {0xAA, 0x00, 0x00, 0x00, 0x00};
        buf[1] = static_cast<uint8_t>(i);
        buf[2] = static_cast<uint8_t>(i + 1);
        buf[3] = static_cast<uint8_t>(i + 2);
        buf[4] = static_cast<uint8_t>(i + 3);
        frames[i] = make_frame(buf, 5, -70);
    }

    PriosAnalyzer::ByteVote votes[PriosFixtureFrame::kMaxBytes]{};
    PriosAnalyzer::compute_byte_votes(frames, N, votes);
    PriosAnalyzer::LengthHistogram hist{};
    PriosAnalyzer::compute_length_histogram(frames, N, hist);

    const auto r = PriosAnalyzer::assess_readiness(frames, N, hist, votes);
    CHECK("readiness_unstable_prefix", !r.stable_prefix);
    CHECK("readiness_not_ready",       !r.ready_for_decoder);
}

// ---- PriosAnalyzer::format_report -------------------------------------------

static void test_format_report_nonempty() {
    constexpr size_t N = 6;
    PriosFixtureFrame frames[N]{};
    build_identical_frames(frames, N, -70);

    PriosAnalyzer::ByteVote votes[PriosFixtureFrame::kMaxBytes]{};
    PriosAnalyzer::compute_byte_votes(frames, N, votes);
    PriosAnalyzer::LengthHistogram hist{};
    PriosAnalyzer::compute_length_histogram(frames, N, hist);
    const auto r = PriosAnalyzer::assess_readiness(frames, N, hist, votes);

    char buf[2048]{};
    const size_t written = PriosAnalyzer::format_report(
        buf, sizeof(buf), frames, N, votes, hist, r);

    CHECK("format_report_written_nonzero",  written > 0);
    CHECK("format_report_nul_terminated",   buf[written] == '\0');

    // Must mention key sections.
    const bool has_frames  = std::strstr(buf, "Frames:") != nullptr;
    const bool has_lengths = std::strstr(buf, "Lengths:") != nullptr;
    const bool has_prefix  = std::strstr(buf, "Stable prefix") != nullptr;
    const bool has_ready   = std::strstr(buf, "READY FOR DECODER") != nullptr;
    CHECK("format_report_has_frames",  has_frames);
    CHECK("format_report_has_lengths", has_lengths);
    CHECK("format_report_has_prefix",  has_prefix);
    CHECK("format_report_has_ready",   has_ready);
}

static void test_format_report_null_buf() {
    const size_t written = PriosAnalyzer::format_report(
        nullptr, 0, nullptr, 0, nullptr,
        PriosAnalyzer::LengthHistogram{}, PriosAnalyzer::Readiness{});
    CHECK("format_report_null_buf_zero", written == 0);
}

static void test_format_report_empty_suite() {
    char buf[256]{};
    const size_t written = PriosAnalyzer::format_report(
        buf, sizeof(buf), nullptr, 0, nullptr,
        PriosAnalyzer::LengthHistogram{}, PriosAnalyzer::Readiness{});
    CHECK("format_report_empty_written_nonzero", written > 0);
    const bool has_no_captures = std::strstr(buf, "no captures") != nullptr;
    CHECK("format_report_empty_has_no_captures", has_no_captures);
}

// ---- PriosAnalyzer::extract_fingerprint ------------------------------------

static void test_extract_fingerprint_valid_long_capture() {
    // Build a frame with exactly 15 bytes (kOffset=9, kLength=6 → need >= 15).
    uint8_t buf[15]{};
    for (uint8_t i = 0; i < 15; ++i) { buf[i] = i; }

    const auto fp = PriosAnalyzer::extract_fingerprint(buf, 15);

    CHECK("fp_valid_at_15_bytes",  fp.valid);
    // Expect bytes 9..14 = {0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E}
    bool bytes_ok = true;
    for (uint8_t i = 0; i < PriosDeviceFingerprint::kLength; ++i) {
        if (fp.bytes[i] != static_cast<uint8_t>(PriosDeviceFingerprint::kOffset + i)) {
            bytes_ok = false;
        }
    }
    CHECK("fp_bytes_9_to_14", bytes_ok);
}

static void test_extract_fingerprint_valid_64_byte_capture() {
    uint8_t buf[64]{};
    for (uint8_t i = 0; i < 64; ++i) { buf[i] = static_cast<uint8_t>(i * 3); }

    const auto fp = PriosAnalyzer::extract_fingerprint(buf, 64);
    CHECK("fp_valid_64_bytes", fp.valid);
    for (uint8_t i = 0; i < PriosDeviceFingerprint::kLength; ++i) {
        const uint8_t expected = static_cast<uint8_t>((PriosDeviceFingerprint::kOffset + i) * 3);
        if (fp.bytes[i] != expected) {
            FAIL("fp_64_byte_value", "byte %u: got %02X expected %02X",
                 static_cast<unsigned>(i),
                 static_cast<unsigned>(fp.bytes[i]),
                 static_cast<unsigned>(expected));
            return;
        }
    }
    PASS("fp_64_byte_values_correct");
}

static void test_extract_fingerprint_too_short() {
    uint8_t buf[14]{};
    const auto fp = PriosAnalyzer::extract_fingerprint(buf, 14);
    CHECK("fp_invalid_at_14_bytes", !fp.valid);
}

static void test_extract_fingerprint_zero_length() {
    uint8_t buf[4]{};
    const auto fp = PriosAnalyzer::extract_fingerprint(buf, 0);
    CHECK("fp_invalid_zero_len", !fp.valid);
}

static void test_extract_fingerprint_null_ptr() {
    const auto fp = PriosAnalyzer::extract_fingerprint(nullptr, 64);
    CHECK("fp_invalid_null_ptr", !fp.valid);
}

static void test_extract_fingerprint_matches_same_device() {
    uint8_t a[15]{};
    uint8_t b[15]{};
    // Fill identically.
    for (uint8_t i = 0; i < 15; ++i) { a[i] = b[i] = i; }

    const auto fp_a = PriosAnalyzer::extract_fingerprint(a, 15);
    const auto fp_b = PriosAnalyzer::extract_fingerprint(b, 15);
    CHECK("fp_matches_same",  fp_a.valid && fp_b.valid && fp_a.matches(fp_b));
}

static void test_extract_fingerprint_no_match_different_device() {
    uint8_t a[15]{};
    uint8_t b[15]{};
    for (uint8_t i = 0; i < 15; ++i) { a[i] = i; b[i] = i; }
    b[10] = 0xFF;  // one of the fingerprint bytes differs

    const auto fp_a = PriosAnalyzer::extract_fingerprint(a, 15);
    const auto fp_b = PriosAnalyzer::extract_fingerprint(b, 15);
    CHECK("fp_no_match_different", !fp_a.matches(fp_b));
}

static void test_format_report_shows_device_section() {
    // Build 3 frames: 2 from device A (bytes 9-14 = 0x01..0x06),
    //                 1 from device B (bytes 9-14 = 0xFF..0xFA).
    constexpr uint8_t kLen = 20;
    PriosFixtureFrame frames[3]{};
    for (int i = 0; i < 3; ++i) {
        frames[i].length = kLen;
        frames[i].rssi_dbm = -70;
    }
    // Device A fingerprint at bytes 9-14
    for (uint8_t b = 0; b < PriosDeviceFingerprint::kLength; ++b) {
        frames[0].bytes[PriosDeviceFingerprint::kOffset + b] = static_cast<uint8_t>(b + 1);
        frames[1].bytes[PriosDeviceFingerprint::kOffset + b] = static_cast<uint8_t>(b + 1);
    }
    // Device B fingerprint
    for (uint8_t b = 0; b < PriosDeviceFingerprint::kLength; ++b) {
        frames[2].bytes[PriosDeviceFingerprint::kOffset + b] = static_cast<uint8_t>(0xFF - b);
    }

    PriosAnalyzer::ByteVote votes[PriosFixtureFrame::kMaxBytes]{};
    PriosAnalyzer::compute_byte_votes(frames, 3, votes);
    PriosAnalyzer::LengthHistogram hist{};
    PriosAnalyzer::compute_length_histogram(frames, 3, hist);
    const auto r = PriosAnalyzer::assess_readiness(frames, 3, hist, votes);

    char buf[4096]{};
    const size_t written = PriosAnalyzer::format_report(buf, sizeof(buf), frames, 3, votes, hist, r);

    CHECK("device_section_written", written > 0);
    const bool has_device_section = std::strstr(buf, "Devices by fingerprint") != nullptr;
    CHECK("format_report_has_device_section", has_device_section);
    // Device A fingerprint should appear (010203040506)
    const bool has_device_a = std::strstr(buf, "010203040506") != nullptr;
    CHECK("format_report_shows_device_a", has_device_a);
}

// ---- main --------------------------------------------------------------------

int main() {
    printf("=== test_prios_fixture_analyzer ===\n");

    // PriosFixtureFrame
    test_from_record_copies_fields();
    test_from_record_no_label();
    test_from_record_truncates_to_max();

    // PriosFixtureSuite
    test_suite_append_and_count();
    test_suite_capacity_limit();

    // compute_byte_votes
    test_byte_votes_stable_prefix();
    test_byte_votes_empty_input();
    test_byte_votes_absent_position();

    // common_prefix_length
    test_common_prefix_length_happy();
    test_common_prefix_length_all_agree();
    test_common_prefix_length_null_input();
    test_common_prefix_length_min_frames_filter();

    // compute_length_histogram
    test_length_histogram_basic();
    test_length_histogram_empty();

    // assess_readiness
    test_readiness_all_criteria_met();
    test_readiness_too_few_captures();
    test_readiness_bad_rssi();
    test_readiness_unstable_prefix();

    // format_report
    test_format_report_nonempty();
    test_format_report_null_buf();
    test_format_report_empty_suite();

    // extract_fingerprint
    test_extract_fingerprint_valid_long_capture();
    test_extract_fingerprint_valid_64_byte_capture();
    test_extract_fingerprint_too_short();
    test_extract_fingerprint_zero_length();
    test_extract_fingerprint_null_ptr();
    test_extract_fingerprint_matches_same_device();
    test_extract_fingerprint_no_match_different_device();
    test_format_report_shows_device_section();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
