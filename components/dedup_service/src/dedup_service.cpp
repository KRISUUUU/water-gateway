#include "dedup_service/dedup_service.hpp"

namespace dedup_service {

DedupService& DedupService::instance() {
    static DedupService svc;
    return svc;
}

bool DedupService::seen_recently(const std::string& key, int64_t now_ms) {
    prune(now_ms);

    for (const auto& entry : entries_) {
        if (entry.key == key) {
            return true;
        }
    }
    return false;
}

void DedupService::remember(const std::string& key, int64_t now_ms) {
    entries_.push_back({key, now_ms});
}

void DedupService::prune(int64_t now_ms) {
    int64_t cutoff = now_ms - window_ms_;
    while (!entries_.empty() && entries_.front().timestamp_ms < cutoff) {
        entries_.pop_front();
    }
}

void DedupService::clear() {
    entries_.clear();
}

} // namespace dedup_service
