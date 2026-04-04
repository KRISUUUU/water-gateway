#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

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
    static constexpr size_t kMaxRecords = 8;

    std::array<PriosCaptureRecord, kMaxRecords> records{};
    size_t   count         = 0;
    uint32_t total_inserted = 0;
    uint32_t total_evicted  = 0;
};

class PriosCaptureService {
  public:
    static PriosCaptureService& instance();

    void insert(const PriosCaptureRecord& record);
    [[nodiscard]] PriosCaptureSnapshot snapshot() const;
    void clear();

  private:
    PriosCaptureService() = default;

    static constexpr size_t kCapacity = PriosCaptureSnapshot::kMaxRecords;

    mutable std::mutex mutex_{};
    std::array<PriosCaptureRecord, kCapacity> storage_{};
    size_t   head_           = 0;
    size_t   count_          = 0;
    uint32_t total_inserted_ = 0;
    uint32_t total_evicted_  = 0;
};

} // namespace wmbus_prios_rx
