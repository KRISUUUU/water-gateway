#include "wmbus_prios_rx/prios_capture_service.hpp"
#include "wmbus_prios_rx/prios_analyzer.hpp"

#include <cstring>
#include <new>

namespace wmbus_prios_rx {

namespace {

uint8_t bounded_variant_b_prefix_length(const PriosCaptureRecord& record) {
    constexpr size_t kPrefixBytes = 6;
    const size_t available =
        record.total_bytes_captured < kPrefixBytes
            ? record.total_bytes_captured
            : kPrefixBytes;
    return static_cast<uint8_t>(available);
}

} // namespace

PriosCaptureService& PriosCaptureService::instance() {
    static PriosCaptureService s_instance;
    return s_instance;
}

void PriosCaptureService::insert(const PriosCaptureRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    insert_locked(record);
}

PriosCaptureInsertDecision PriosCaptureService::insert_with_quality_gate(
    const PriosCaptureRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!record.manchester_enabled) {
        insert_locked(record);
        return PriosCaptureInsertDecision::Inserted;
    }

    const bool seen_before = variant_b_prefix_seen_locked(record);
    remember_variant_b_prefix_locked(record);
    if (!seen_before) {
        total_similarity_rejected_++;
        return PriosCaptureInsertDecision::RejectedVariantBSimilarity;
    }

    insert_locked(record);
    return PriosCaptureInsertDecision::Inserted;
}

void PriosCaptureService::record_burst_start() {
    std::lock_guard<std::mutex> lock(mutex_);
    total_burst_starts_++;
}

void PriosCaptureService::record_sync_campaign_start() {
    std::lock_guard<std::mutex> lock(mutex_);
    total_sync_campaign_starts_++;
}

void PriosCaptureService::record_noise_rejection(bool manchester_enabled, bool short_capture) {
    std::lock_guard<std::mutex> lock(mutex_);
    total_noise_rejected_++;
    if (manchester_enabled && short_capture) {
        variant_b_short_rejected_++;
    }
}

void PriosCaptureService::record_quality_rejection() {
    std::lock_guard<std::mutex> lock(mutex_);
    total_quality_rejected_++;
}

void PriosCaptureService::insert_locked(const PriosCaptureRecord& record) {
    const size_t idx = head_ % kCapacity;
    if (count_ == kCapacity) {
        total_evicted_++;
    } else {
        count_++;
    }
    storage_[idx] = record;
    head_         = (head_ + 1) % kCapacity;
    total_inserted_++;
    retained_length_total_ += record.total_bytes_captured;
    if (total_inserted_ == 1U) {
        retained_length_min_ = record.total_bytes_captured;
        retained_length_max_ = record.total_bytes_captured;
    } else {
        if (record.total_bytes_captured < retained_length_min_) {
            retained_length_min_ = record.total_bytes_captured;
        }
        if (record.total_bytes_captured > retained_length_max_) {
            retained_length_max_ = record.total_bytes_captured;
        }
    }
    if (record.manchester_enabled) {
        retained_variant_b_total_++;
    } else {
        retained_variant_a_total_++;
    }
}

bool PriosCaptureService::variant_b_prefix_seen_locked(const PriosCaptureRecord& record) const {
    const uint8_t prefix_len = bounded_variant_b_prefix_length(record);
    if (prefix_len == 0) {
        return false;
    }

    for (size_t i = 0; i < variant_b_observation_count_; ++i) {
        const auto& obs = variant_b_observations_[i];
        if (obs.length != prefix_len) {
            continue;
        }
        if (std::memcmp(obs.bytes, record.captured_bytes, prefix_len) == 0) {
            return true;
        }
    }
    return false;
}

void PriosCaptureService::remember_variant_b_prefix_locked(const PriosCaptureRecord& record) {
    VariantBPrefixObservation obs{};
    obs.length = bounded_variant_b_prefix_length(record);
    if (obs.length > 0) {
        std::memcpy(obs.bytes, record.captured_bytes, obs.length);
    }

    const size_t idx = variant_b_observation_head_ % kVariantBObservationDepth;
    variant_b_observations_[idx] = obs;
    variant_b_observation_head_ = (variant_b_observation_head_ + 1) % kVariantBObservationDepth;
    if (variant_b_observation_count_ < kVariantBObservationDepth) {
        variant_b_observation_count_++;
    }
}

PriosCaptureSnapshot PriosCaptureService::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);

    PriosCaptureSnapshot snap;
    snap.count          = count_;
    snap.total_inserted = total_inserted_;
    snap.total_evicted  = total_evicted_;

    // Reconstruct records in insertion order (oldest first).
    // head_ points to the slot that will be overwritten next.
    // Oldest slot is at (head_ - count_ + kCapacity) % kCapacity.
    const size_t oldest = count_ < kCapacity
                              ? 0
                              : head_ % kCapacity;
    for (size_t i = 0; i < count_; ++i) {
        const size_t src = (oldest + i) % kCapacity;
        snap.records[i]  = storage_[src];
    }

    return snap;
}

std::unique_ptr<PriosCaptureSnapshot> PriosCaptureService::snapshot_allocated() const {
    auto snap = std::unique_ptr<PriosCaptureSnapshot>(new (std::nothrow) PriosCaptureSnapshot{});
    if (!snap) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    snap->count          = count_;
    snap->total_inserted = total_inserted_;
    snap->total_evicted  = total_evicted_;

    const size_t oldest = count_ < kCapacity
                              ? 0
                              : head_ % kCapacity;
    for (size_t i = 0; i < count_; ++i) {
        const size_t src = (oldest + i) % kCapacity;
        snap->records[i] = storage_[src];
    }

    return snap;
}

PriosCaptureStats PriosCaptureService::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    PriosCaptureStats stats;
    stats.count = count_;
    stats.total_inserted = total_inserted_;
    stats.total_evicted = total_evicted_;
    stats.total_burst_starts = total_burst_starts_;
    stats.total_sync_campaign_starts = total_sync_campaign_starts_;
    stats.total_noise_rejected = total_noise_rejected_;
    stats.total_dedup_rejected = total_dedup_rejected_;
    stats.total_device_quota_rejected = total_device_quota_rejected_;
    stats.total_new_device_limit_rejected = total_new_device_limit_rejected_;
    stats.unique_devices_tracked = count_unique_devices_locked();
    stats.total_quality_rejected = total_quality_rejected_;
    stats.variant_b_short_rejected = variant_b_short_rejected_;
    stats.total_similarity_rejected = total_similarity_rejected_;
    stats.retained_variant_a_total = retained_variant_a_total_;
    stats.retained_variant_b_total = retained_variant_b_total_;
    stats.retained_length_total = retained_length_total_;
    stats.retained_length_min = retained_length_min_;
    stats.retained_length_max = retained_length_max_;
    return stats;
}

PriosCapturePreviewSnapshot PriosCaptureService::preview_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);

    PriosCapturePreviewSnapshot snap;
    snap.total_inserted = total_inserted_;
    snap.total_evicted  = total_evicted_;
    snap.count          = count_ < PriosCapturePreviewSnapshot::kMaxRecords
                              ? count_
                              : PriosCapturePreviewSnapshot::kMaxRecords;

    if (snap.count == 0) {
        return snap;
    }

    const size_t first =
        (head_ + kCapacity - snap.count) % kCapacity;
    for (size_t i = 0; i < snap.count; ++i) {
        const auto& src = storage_[(first + i) % kCapacity];
        auto& dst = snap.records[i];
        dst.sequence             = src.sequence;
        dst.timestamp_ms         = src.timestamp_ms;
        dst.rssi_dbm             = src.rssi_dbm;
        dst.lqi                  = src.lqi;
        dst.total_bytes_captured = src.total_bytes_captured;
        dst.manchester_enabled   = src.manchester_enabled;
        const size_t preview_len =
            src.total_bytes_captured < PriosCapturePreviewRecord::kPreviewBytes
                ? src.total_bytes_captured
                : PriosCapturePreviewRecord::kPreviewBytes;
        dst.preview_length = static_cast<uint8_t>(preview_len);
        std::memcpy(dst.preview_bytes, src.captured_bytes, preview_len);
    }

    return snap;
}

// ---- Fingerprint helpers (mutex held by callers) ----------------------------

size_t PriosCaptureService::count_for_fingerprint_locked(const PriosDeviceFingerprint& fp) const {
    constexpr size_t kRequired = PriosDeviceFingerprint::kOffset + PriosDeviceFingerprint::kLength;
    size_t n = 0;
    const size_t oldest = count_ < kCapacity ? 0 : head_ % kCapacity;
    for (size_t i = 0; i < count_; ++i) {
        const auto& rec = storage_[(oldest + i) % kCapacity];
        if (rec.total_bytes_captured < kRequired) {
            continue;
        }
        if (std::memcmp(rec.captured_bytes + PriosDeviceFingerprint::kOffset,
                        fp.bytes, PriosDeviceFingerprint::kLength) == 0) {
            ++n;
        }
    }
    return n;
}

bool PriosCaptureService::is_exact_duplicate_locked(const PriosDeviceFingerprint& fp,
                                                     const PriosCaptureRecord& incoming) const {
    constexpr size_t kRequired = PriosDeviceFingerprint::kOffset + PriosDeviceFingerprint::kLength;
    const size_t oldest = count_ < kCapacity ? 0 : head_ % kCapacity;
    for (size_t i = 0; i < count_; ++i) {
        const auto& rec = storage_[(oldest + i) % kCapacity];
        if (rec.total_bytes_captured < kRequired) {
            continue;
        }
        // Quick fingerprint check via raw byte comparison.
        if (std::memcmp(rec.captured_bytes + PriosDeviceFingerprint::kOffset,
                        fp.bytes, PriosDeviceFingerprint::kLength) != 0) {
            continue;
        }
        // Same device: compare full payload.
        if (rec.total_bytes_captured == incoming.total_bytes_captured &&
            std::memcmp(rec.captured_bytes, incoming.captured_bytes,
                        rec.total_bytes_captured) == 0) {
            return true;
        }
    }
    return false;
}

size_t PriosCaptureService::count_unique_devices_locked() const {
    // Stack-local fingerprint table: bounded by kMaxTrackedDevices so no heap
    // allocation is needed here.
    constexpr size_t kRequired = PriosDeviceFingerprint::kOffset + PriosDeviceFingerprint::kLength;
    uint8_t seen[kMaxTrackedDevices][PriosDeviceFingerprint::kLength]{};
    size_t  seen_count = 0;

    const size_t oldest = count_ < kCapacity ? 0 : head_ % kCapacity;
    for (size_t i = 0; i < count_; ++i) {
        const auto& rec = storage_[(oldest + i) % kCapacity];
        if (rec.total_bytes_captured < kRequired) {
            continue;
        }
        const uint8_t* fp_bytes = rec.captured_bytes + PriosDeviceFingerprint::kOffset;
        bool found = false;
        for (size_t j = 0; j < seen_count; ++j) {
            if (std::memcmp(seen[j], fp_bytes, PriosDeviceFingerprint::kLength) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            if (seen_count < kMaxTrackedDevices) {
                std::memcpy(seen[seen_count++], fp_bytes, PriosDeviceFingerprint::kLength);
            }
            // If we've already filled the table we know we're at the limit — stop early.
            if (seen_count == kMaxTrackedDevices) {
                break;
            }
        }
    }
    return seen_count;
}

// ---- insert_with_dedup_gate --------------------------------------------------

PriosCaptureInsertDecision PriosCaptureService::insert_with_dedup_gate(
    const PriosCaptureRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto fp = PriosAnalyzer::extract_fingerprint(record.captured_bytes,
                                                        record.total_bytes_captured);
    if (fp.valid) {
        // Reject exact payload duplicates (same meter, same reading already stored).
        if (is_exact_duplicate_locked(fp, record)) {
            total_dedup_rejected_++;
            return PriosCaptureInsertDecision::RejectedDuplicate;
        }
        const size_t existing = count_for_fingerprint_locked(fp);
        // Enforce per-device retention cap so no single meter floods the buffer.
        if (existing >= kMaxRecordsPerDevice) {
            total_device_quota_rejected_++;
            return PriosCaptureInsertDecision::RejectedDeviceQuota;
        }
        // Protect global RAM: if this is a brand-new device and the global table is full, drop it.
        if (existing == 0 && count_unique_devices_locked() >= kMaxTrackedDevices) {
            total_new_device_limit_rejected_++;
            return PriosCaptureInsertDecision::RejectedNewDeviceLimit;
        }
    }
    // Fingerprint absent (capture too short) or passes all checks: insert.
    insert_locked(record);
    return PriosCaptureInsertDecision::Inserted;
}

void PriosCaptureService::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    storage_      = {};
    head_         = 0;
    count_        = 0;
    variant_b_observations_ = {};
    variant_b_observation_head_ = 0;
    variant_b_observation_count_ = 0;
    total_inserted_ = 0;
    total_evicted_  = 0;
    total_burst_starts_ = 0;
    total_sync_campaign_starts_ = 0;
    total_noise_rejected_ = 0;
    total_dedup_rejected_ = 0;
    total_device_quota_rejected_ = 0;
    total_new_device_limit_rejected_ = 0;
    total_quality_rejected_ = 0;
    variant_b_short_rejected_ = 0;
    total_similarity_rejected_ = 0;
    retained_variant_a_total_ = 0;
    retained_variant_b_total_ = 0;
    retained_length_total_ = 0;
    retained_length_min_ = 0;
    retained_length_max_ = 0;
}

} // namespace wmbus_prios_rx
