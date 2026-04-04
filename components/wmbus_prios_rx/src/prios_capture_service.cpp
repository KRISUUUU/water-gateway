#include "wmbus_prios_rx/prios_capture_service.hpp"

#include <cstring>

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

void PriosCaptureService::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    storage_      = {};
    head_         = 0;
    count_        = 0;
    total_inserted_ = 0;
    total_evicted_  = 0;
}

} // namespace wmbus_prios_rx
