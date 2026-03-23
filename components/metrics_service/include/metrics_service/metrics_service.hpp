#pragma once

#include <cstdint>

namespace metrics_service {

struct RuntimeMetrics {
    std::uint32_t uptime_seconds{0};
    std::uint32_t free_heap_bytes{0};
};

class MetricsService {
public:
    static MetricsService& instance();

    void initialize();
    [[nodiscard]] RuntimeMetrics snapshot() const;

private:
    MetricsService() = default;
};

}  // namespace metrics_service
