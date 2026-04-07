#include "wmbus_prios_rx/prios_analyzer.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace wmbus_prios_rx {

// ---- ByteVote ---------------------------------------------------------------

void PriosAnalyzer::compute_byte_votes(const PriosFixtureFrame* frames,
                                        size_t                   count,
                                        ByteVote*                out_votes) {
    if (!frames || !out_votes || count == 0) {
        return;
    }

    // Per-position value frequency table (256 values).
    // Process one position at a time to avoid a 64×256 stack allocation.
    for (size_t pos = 0; pos < PriosFixtureFrame::kMaxBytes; ++pos) {
        uint8_t freq[256]{};
        uint8_t present = 0;

        for (size_t fi = 0; fi < count; ++fi) {
            if (pos < frames[fi].length) {
                freq[frames[fi].bytes[pos]]++;
                present++;
            }
        }

        ByteVote& v = out_votes[pos];
        v.present_count = present;

        if (present == 0) {
            v.dominant_value = 0;
            v.dominant_count = 0;
            v.agreement_pct  = 0;
            continue;
        }

        uint8_t best_val   = 0;
        uint8_t best_count = 0;
        for (uint16_t byte_val = 0; byte_val < 256; ++byte_val) {
            if (freq[byte_val] > best_count) {
                best_count = freq[byte_val];
                best_val   = static_cast<uint8_t>(byte_val);
            }
        }

        v.dominant_value = best_val;
        v.dominant_count = best_count;
        v.agreement_pct  = static_cast<uint8_t>(
            (static_cast<uint32_t>(best_count) * 100U) / present);
    }
}

// ---- Common prefix ----------------------------------------------------------

size_t PriosAnalyzer::common_prefix_length(const ByteVote* votes,
                                             size_t          num_positions,
                                             uint8_t         min_agreement_pct,
                                             uint8_t         min_frames) {
    if (!votes || num_positions == 0) {
        return 0;
    }

    for (size_t pos = 0; pos < num_positions; ++pos) {
        const ByteVote& v = votes[pos];
        if (v.present_count < min_frames) {
            return pos;
        }
        if (v.agreement_pct < min_agreement_pct) {
            return pos;
        }
    }

    return num_positions;
}

// ---- Length histogram -------------------------------------------------------

void PriosAnalyzer::compute_length_histogram(const PriosFixtureFrame* frames,
                                               size_t                   count,
                                               LengthHistogram&         out_hist) {
    out_hist = LengthHistogram{};

    if (!frames || count == 0) {
        return;
    }

    out_hist.total_frames = count;
    out_hist.min_length   = 0xFF;
    out_hist.max_length   = 0;

    for (size_t i = 0; i < count; ++i) {
        const uint8_t len = frames[i].length;
        if (len <= PriosFixtureFrame::kMaxBytes) {
            out_hist.counts[len]++;
        }
        if (len < out_hist.min_length) {
            out_hist.min_length = len;
        }
        if (len > out_hist.max_length) {
            out_hist.max_length = len;
        }
    }

    uint8_t modal_len   = 0;
    uint8_t modal_count = 0;
    for (size_t len = 0; len <= PriosFixtureFrame::kMaxBytes; ++len) {
        if (out_hist.counts[len] > modal_count) {
            modal_count = out_hist.counts[len];
            modal_len   = static_cast<uint8_t>(len);
        }
    }
    out_hist.modal_length = modal_len;
    out_hist.modal_count  = modal_count;
}

// ---- Decoder readiness ------------------------------------------------------

PriosAnalyzer::Readiness PriosAnalyzer::assess_readiness(
    const PriosFixtureFrame* frames,
    size_t                   count,
    const LengthHistogram&   hist,
    const ByteVote*          votes) {

    Readiness r{};
    r.capture_count        = count;
    r.agreement_pct_used   = kPrefixAgreementPct;

    // Enough captures?
    r.enough_captures = count >= kMinCaptures;

    // Stable prefix?
    const size_t cpl = common_prefix_length(votes,
                                             PriosFixtureFrame::kMaxBytes,
                                             kPrefixAgreementPct,
                                             1);
    r.prefix_length  = cpl;
    r.stable_prefix  = cpl >= kMinPrefixBytes;

    // Consistent lengths?
    if (count > 0 && hist.modal_count > 0) {
        const uint8_t pct = static_cast<uint8_t>(
            (static_cast<uint32_t>(hist.modal_count) * 100U) / count);
        r.length_consistency_pct = pct;
        r.consistent_lengths     = pct >= kLengthConsistencyPct;
    }

    // Good RSSI? (find best across all frames)
    int8_t best_rssi = -128;
    if (frames) {
        for (size_t i = 0; i < count; ++i) {
            if (frames[i].rssi_dbm > best_rssi) {
                best_rssi = frames[i].rssi_dbm;
            }
        }
    }
    r.best_rssi_dbm = best_rssi;
    r.good_rssi     = best_rssi > kMinRssiDbm;

    r.ready_for_decoder = r.enough_captures  &&
                          r.stable_prefix    &&
                          r.consistent_lengths &&
                          r.good_rssi;
    return r;
}

// ---- Device fingerprint extraction ------------------------------------------

PriosDeviceFingerprint PriosAnalyzer::extract_fingerprint(const uint8_t* data, size_t len) {
    PriosDeviceFingerprint fp{};
    constexpr size_t kRequired =
        PriosDeviceFingerprint::kOffset + PriosDeviceFingerprint::kLength;
    if (!data || len < kRequired) {
        return fp;  // valid = false
    }
    std::memcpy(fp.bytes, data + PriosDeviceFingerprint::kOffset,
                PriosDeviceFingerprint::kLength);
    fp.valid = true;
    return fp;
}

// ---- Text report ------------------------------------------------------------

size_t PriosAnalyzer::format_report(char*                    buf,
                                     size_t                   buf_size,
                                     const PriosFixtureFrame* frames,
                                     size_t                   count,
                                     const ByteVote*          votes,
                                     const LengthHistogram&   hist,
                                     const Readiness&         readiness) {
    if (!buf || buf_size == 0) {
        return 0;
    }
    buf[0] = '\0';

    size_t off = 0;

    // Use a raw snprintf approach.
#define EMIT(...)                                                                     \
    do {                                                                              \
        int _n = std::snprintf(buf + off, buf_size > off ? buf_size - off : 0,       \
                               __VA_ARGS__);                                          \
        if (_n > 0) off += static_cast<size_t>(_n);                                  \
        if (off >= buf_size) { off = buf_size - 1; goto done; }                      \
    } while (0)

    EMIT("=== PRIOS R3 Fixture Analysis ===\n");
    EMIT("Frames: %zu\n", count);

    if (count == 0) {
        EMIT("(no captures)\n");
        goto done;
    }

    // Length distribution
    EMIT("Lengths: min=%u max=%u modal=%u (%u/%zu, %u%%)\n",
         static_cast<unsigned>(hist.min_length),
         static_cast<unsigned>(hist.max_length),
         static_cast<unsigned>(hist.modal_length),
         static_cast<unsigned>(hist.modal_count),
         count,
         static_cast<unsigned>(count > 0
             ? static_cast<unsigned>(hist.modal_count) * 100U /
                   static_cast<unsigned>(count)
             : 0U));

    // Common prefix
    EMIT("Stable prefix (%u%% agreement): %zu bytes\n",
         kPrefixAgreementPct, readiness.prefix_length);

    if (readiness.prefix_length > 0 && votes) {
        EMIT("Prefix bytes:");
        for (size_t i = 0; i < readiness.prefix_length && i < PriosFixtureFrame::kMaxBytes; ++i) {
            EMIT(" %02X(%u%%)", votes[i].dominant_value, votes[i].agreement_pct);
        }
        EMIT("\n");
    }

    // Per-frame summary (first bytes + RSSI)
    EMIT("Per-frame summary:\n");
    if (frames) {
        for (size_t i = 0; i < count; ++i) {
            const auto& f = frames[i];
            EMIT("  [%zu] len=%-3u rssi=%-4d lqi=%-3u  ",
                 i, f.length, f.rssi_dbm, f.lqi);
            const size_t show = f.length < 8u ? f.length : 8u;
            for (size_t b = 0; b < show; ++b) {
                EMIT("%02X", f.bytes[b]);
            }
            if (f.length > 8) {
                EMIT("..(%u)", f.length - 8);
            }
            if (f.label[0]) {
                EMIT("  [%s]", f.label);
            }
            EMIT("\n");
        }
    }

    // Per-device fingerprint grouping
    EMIT("Devices by fingerprint (bytes %u-%u):\n",
         static_cast<unsigned>(PriosDeviceFingerprint::kOffset),
         static_cast<unsigned>(PriosDeviceFingerprint::kOffset +
                               PriosDeviceFingerprint::kLength - 1U));
    if (frames && count > 0) {
        // Stack-allocated fingerprint frequency table; bounded by kMaxFpEntries.
        struct FpEntry {
            uint8_t bytes[PriosDeviceFingerprint::kLength]{};
            uint8_t count = 0;
        };
        constexpr size_t kMaxFpEntries = 16;
        FpEntry fp_table[kMaxFpEntries]{};
        size_t  fp_count = 0;
        size_t  short_count = 0;  // captures too short to fingerprint

        for (size_t i = 0; i < count; ++i) {
            const auto fp = extract_fingerprint(frames[i].bytes, frames[i].length);
            if (!fp.valid) {
                ++short_count;
                continue;
            }
            bool found = false;
            for (size_t j = 0; j < fp_count; ++j) {
                if (std::memcmp(fp_table[j].bytes, fp.bytes,
                                PriosDeviceFingerprint::kLength) == 0) {
                    if (fp_table[j].count < 0xFF) {
                        fp_table[j].count++;
                    }
                    found = true;
                    break;
                }
            }
            if (!found && fp_count < kMaxFpEntries) {
                std::memcpy(fp_table[fp_count].bytes, fp.bytes,
                            PriosDeviceFingerprint::kLength);
                fp_table[fp_count].count = 1;
                fp_count++;
            }
        }

        if (fp_count == 0 && short_count == count) {
            EMIT("  (all captures too short — need >= %u bytes)\n",
                 static_cast<unsigned>(PriosDeviceFingerprint::kOffset +
                                       PriosDeviceFingerprint::kLength));
        } else {
            for (size_t i = 0; i < fp_count; ++i) {
                EMIT("  [%zu] ", i);
                for (size_t b = 0; b < PriosDeviceFingerprint::kLength; ++b) {
                    EMIT("%02X", fp_table[i].bytes[b]);
                }
                EMIT("  count=%u\n", static_cast<unsigned>(fp_table[i].count));
            }
            if (fp_count >= kMaxFpEntries) {
                EMIT("  (table full at %zu devices — more may be present)\n",
                     kMaxFpEntries);
            }
            if (short_count > 0) {
                EMIT("  (%zu capture(s) too short to fingerprint)\n", short_count);
            }
        }
    } else {
        EMIT("  (no captures)\n");
    }

    // Readiness summary
    EMIT("Decoder readiness:\n");
    EMIT("  enough_captures    : %s (%zu >= %zu)\n",
         readiness.enough_captures    ? "YES" : "NO",
         count, kMinCaptures);
    EMIT("  stable_prefix      : %s (%zu >= %zu bytes)\n",
         readiness.stable_prefix      ? "YES" : "NO",
         readiness.prefix_length, kMinPrefixBytes);
    EMIT("  consistent_lengths : %s (%u%% >= %u%%)\n",
         readiness.consistent_lengths ? "YES" : "NO",
         readiness.length_consistency_pct, kLengthConsistencyPct);
    EMIT("  good_rssi          : %s (best=%ddBm > %ddBm)\n",
         readiness.good_rssi          ? "YES" : "NO",
         readiness.best_rssi_dbm, kMinRssiDbm);
    EMIT("  READY FOR DECODER  : %s\n",
         readiness.ready_for_decoder  ? "YES" : "NO — collect more captures");

#undef EMIT
done:
    if (off < buf_size) {
        buf[off] = '\0';
    } else {
        buf[buf_size - 1] = '\0';
        off = buf_size - 1;
    }
    return off;
}

} // namespace wmbus_prios_rx
