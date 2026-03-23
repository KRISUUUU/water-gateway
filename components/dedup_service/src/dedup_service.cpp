#include "dedup_service/dedup_service.hpp"

namespace dedup_service {

DedupService::DedupService(common::Milliseconds window_ms)
    : window_ms_(window_ms) {
}

bool DedupService::seen_recently(const std::string& key, common::TimestampMs now_ms) {
    prune(now_ms);

    for (const auto& entry : entries_) {
        if (entry.key == key) {
            return true;
        }
    }

    return false;
}

void DedupService::remember(const std::string& key, common::TimestampMs now_ms) {
    prune(now_ms);
    entries_.push_back({key, now_ms});
}

void DedupService::prune(common::TimestampMs now_ms) {
    while (!entries_.empty()) {
        const auto age = now_ms - entries_.front().timestamp_ms;
        if (age <= window_ms_) {
            break;
        }
        entries_.pop_front();
    }
}

}  // namespace dedup_service
