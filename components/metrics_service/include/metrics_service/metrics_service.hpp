#pragma once

#include "common/result.hpp"
#include <cstdint>

namespace metrics_service {

/// Heap and uptime figures sampled from ESP-IDF (see snapshot()).
struct RuntimeMetrics {
    std::uint32_t uptime_s{0};
    std::uint32_t free_heap_bytes{0};
    std::uint32_t min_free_heap_bytes{0};
    std::uint32_t largest_free_block{0};
};

class MetricsService {
public:
    static MetricsService& instance();

    [[nodiscard]] common::Result<RuntimeMetrics> snapshot() const;

private:
    MetricsService() = default;
};

} // namespace metrics_service
