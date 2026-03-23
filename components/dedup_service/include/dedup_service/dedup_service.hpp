#pragma once

#include <cstdint>
#include <deque>
#include <string>

namespace dedup_service {

struct DedupEntry {
    std::string key;
    int64_t timestamp_ms;
};

// Sliding-window duplicate detection.
// Entries older than the configured window are pruned on each check.
// Thread safety: not thread-safe; caller must ensure single-threaded access
// (pipeline_task is the only consumer).
class DedupService {
public:
    static DedupService& instance();

    // Set dedup window in milliseconds. Default: 5000 (5 seconds).
    void set_window_ms(int64_t window_ms) { window_ms_ = window_ms; }

    // Check if a key was seen within the dedup window.
    // Also prunes expired entries.
    bool seen_recently(const std::string& key, int64_t now_ms);

    // Remember a key with current timestamp.
    void remember(const std::string& key, int64_t now_ms);

    // Remove entries older than the window.
    void prune(int64_t now_ms);

    // Number of entries currently tracked
    size_t entry_count() const { return entries_.size(); }

    // Clear all entries
    void clear();

private:
    DedupService() = default;

    int64_t window_ms_ = 5000;
    std::deque<DedupEntry> entries_;
};

} // namespace dedup_service
