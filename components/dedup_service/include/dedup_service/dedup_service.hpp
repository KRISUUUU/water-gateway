#pragma once

#include <deque>
#include <string>

#include "common/types.hpp"

namespace dedup_service {

struct DedupEntry {
    std::string key{};
    common::TimestampMs timestamp_ms{0};
};

class DedupService {
public:
    explicit DedupService(common::Milliseconds window_ms = 60000);

    bool seen_recently(const std::string& key, common::TimestampMs now_ms);
    void remember(const std::string& key, common::TimestampMs now_ms);
    void prune(common::TimestampMs now_ms);

private:
    common::Milliseconds window_ms_;
    std::deque<DedupEntry> entries_;
};

}  // namespace dedup_service
