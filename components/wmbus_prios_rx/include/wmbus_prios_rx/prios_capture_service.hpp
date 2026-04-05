#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <memory>

// PriosCaptureService: bounded ring buffer for raw PRIOS bring-up captures.
//
// Captures are stored here by the radio task whenever the PRIOS bring-up
// session collects a bounded raw capture. The HTTP handler reads them for
// diagnostics visibility.
//
// Thread safety: the ring buffer is mutex-protected. The radio task writes;
// the HTTP handler task reads via snapshot(). Pattern mirrors RfDiagnosticsService.
//
// Each record stores the full bounded capture window produced by the bring-up
// session. The capture remains bounded so the export path stays practical for
// embedded use while preserving enough evidence for offline analysis.

namespace wmbus_prios_rx {

struct PriosCaptureRecord {
    // Bounded raw capture copied from the PRIOS bring-up session.
    // Matches PriosBringUpSession::kMaxCaptureBytes and PriosFixtureFrame::kMaxBytes.
    static constexpr size_t kMaxCaptureBytes = 64;
    static constexpr size_t kDisplayPrefixBytes = 32;

    uint32_t sequence            = 0;
    int64_t  timestamp_ms        = 0;  // monotonic or epoch (whichever is available)
    int8_t   rssi_dbm            = 0;
    uint8_t  lqi                 = 0;
    bool     radio_crc_ok        = false;
    bool     radio_crc_available = false;
    uint16_t total_bytes_captured = 0;  // how many bytes were captured in this bounded record
    uint8_t  captured_bytes[kMaxCaptureBytes]{};

    // Which PRIOS capture variant was active when this record was captured.
    // false = Variant A (Manchester off), true = Variant B (Manchester on).
    // Needed to compare captures across variants during offline analysis.
    bool manchester_enabled = false;
};

struct PriosCaptureSnapshot {
    static constexpr size_t kMaxRecords = 64;

    std::array<PriosCaptureRecord, kMaxRecords> records{};
    size_t   count         = 0;
    uint32_t total_inserted = 0;
    uint32_t total_evicted  = 0;
};

struct PriosCaptureStats {
    size_t   count = 0;
    uint32_t total_inserted = 0;
    uint32_t total_evicted  = 0;
    uint32_t total_burst_starts = 0;
    // Sessions started specifically by the sync-word trigger in SyncCampaign mode.
    // Complements total_burst_starts (which counts all modes).  A rising value
    // here confirms that the current sync candidate (0x1E9B) is being recognised
    // by the CC1101 hardware decoder.
    uint32_t total_sync_campaign_starts = 0;
    uint32_t total_noise_rejected = 0;
    uint32_t total_quality_rejected = 0;
    uint32_t variant_b_short_rejected = 0;
    uint32_t total_similarity_rejected = 0;
    uint32_t retained_variant_a_total = 0;
    uint32_t retained_variant_b_total = 0;
    uint32_t retained_length_total = 0;
    uint16_t retained_length_min = 0;
    uint16_t retained_length_max = 0;
};

enum class PriosCaptureInsertDecision : uint8_t {
    Inserted = 0,
    RejectedVariantBSimilarity,
};

struct PriosCapturePreviewRecord {
    static constexpr size_t kPreviewBytes = PriosCaptureRecord::kDisplayPrefixBytes;

    uint32_t sequence             = 0;
    int64_t  timestamp_ms         = 0;
    int8_t   rssi_dbm             = 0;
    uint8_t  lqi                  = 0;
    uint16_t total_bytes_captured = 0;
    bool     manchester_enabled   = false;
    uint8_t  preview_length       = 0;
    uint8_t  preview_bytes[kPreviewBytes]{};
};

struct PriosCapturePreviewSnapshot {
    static constexpr size_t kMaxRecords = 8;

    std::array<PriosCapturePreviewRecord, kMaxRecords> records{};
    size_t   count          = 0;
    uint32_t total_inserted = 0;
    uint32_t total_evicted  = 0;
};

class PriosCaptureService {
  public:
    static PriosCaptureService& instance();

    void insert(const PriosCaptureRecord& record);
    [[nodiscard]] PriosCaptureInsertDecision insert_with_quality_gate(
        const PriosCaptureRecord& record);
    void record_burst_start();
    void record_sync_campaign_start();
    void record_noise_rejection(bool manchester_enabled, bool short_capture);
    void record_quality_rejection();
    [[nodiscard]] PriosCaptureSnapshot snapshot() const;
    [[nodiscard]] std::unique_ptr<PriosCaptureSnapshot> snapshot_allocated() const;
    [[nodiscard]] PriosCaptureStats stats() const;
    [[nodiscard]] PriosCapturePreviewSnapshot preview_snapshot() const;
    void clear();

  private:
    PriosCaptureService() = default;

    static constexpr size_t kCapacity = PriosCaptureSnapshot::kMaxRecords;
    static constexpr size_t kVariantBSimilarityPrefixBytes = 6;
    static constexpr size_t kVariantBObservationDepth = 16;

    struct VariantBPrefixObservation {
        uint8_t length = 0;
        uint8_t bytes[kVariantBSimilarityPrefixBytes]{};
    };

    void insert_locked(const PriosCaptureRecord& record);
    [[nodiscard]] bool variant_b_prefix_seen_locked(const PriosCaptureRecord& record) const;
    void remember_variant_b_prefix_locked(const PriosCaptureRecord& record);

    mutable std::mutex mutex_{};
    std::array<PriosCaptureRecord, kCapacity> storage_{};
    std::array<VariantBPrefixObservation, kVariantBObservationDepth> variant_b_observations_{};
    size_t   head_           = 0;
    size_t   count_          = 0;
    size_t   variant_b_observation_head_ = 0;
    size_t   variant_b_observation_count_ = 0;
    uint32_t total_inserted_ = 0;
    uint32_t total_evicted_  = 0;
    uint32_t total_burst_starts_ = 0;
    uint32_t total_sync_campaign_starts_ = 0;
    uint32_t total_noise_rejected_ = 0;
    uint32_t total_quality_rejected_ = 0;
    uint32_t variant_b_short_rejected_ = 0;
    uint32_t total_similarity_rejected_ = 0;
    uint32_t retained_variant_a_total_ = 0;
    uint32_t retained_variant_b_total_ = 0;
    uint32_t retained_length_total_ = 0;
    uint16_t retained_length_min_ = 0;
    uint16_t retained_length_max_ = 0;
};

} // namespace wmbus_prios_rx
