#include "dedup_service/dedup_service.hpp"

namespace dedup_service {

DedupService& DedupService::instance() {
    static DedupService svc;
    return svc;
}

bool DedupService::seen_recently(const std::string& key, int64_t now_ms) {
    prune(now_ms);

    auto it = key_index_.find(key);
    return it != key_index_.end() && it->second > 0;
}

void DedupService::remember(const std::string& key, int64_t now_ms) {
    // Enforce hard cap to prevent unbounded memory growth.
    while (entries_.size() >= kMaxEntries) {
        evict_oldest();
    }
    entries_.push_back({key, now_ms});
    key_index_[key]++;
}

void DedupService::prune(int64_t now_ms) {
    int64_t cutoff = now_ms - window_ms_;
    while (!entries_.empty() && entries_.front().timestamp_ms < cutoff) {
        evict_oldest();
    }
}

void DedupService::evict_oldest() {
    if (entries_.empty()) {
        return;
    }
    const auto& front = entries_.front();
    auto it = key_index_.find(front.key);
    if (it != key_index_.end()) {
        if (it->second <= 1) {
            key_index_.erase(it);
        } else {
            it->second--;
        }
    }
    entries_.pop_front();
}

void DedupService::clear() {
    entries_.clear();
    key_index_.clear();
}

} // namespace dedup_service
