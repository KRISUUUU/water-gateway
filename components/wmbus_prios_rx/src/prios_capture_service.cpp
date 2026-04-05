#include "wmbus_prios_rx/prios_capture_service.hpp"

#include <cstring>
#include <new>

namespace wmbus_prios_rx {

PriosCaptureService& PriosCaptureService::instance() {
    static PriosCaptureService s_instance;
    return s_instance;
}

void PriosCaptureService::insert(const PriosCaptureRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);

    const size_t idx = head_ % kCapacity;
    if (count_ == kCapacity) {
        total_evicted_++;
    } else {
        count_++;
    }
    storage_[idx] = record;
    head_         = (head_ + 1) % kCapacity;
    total_inserted_++;
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

void PriosCaptureService::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    storage_      = {};
    head_         = 0;
    count_        = 0;
    total_inserted_ = 0;
    total_evicted_  = 0;
}

} // namespace wmbus_prios_rx
