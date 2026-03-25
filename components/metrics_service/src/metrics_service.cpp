#include "metrics_service/metrics_service.hpp"

#ifndef HOST_TEST_BUILD
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#endif

namespace metrics_service {

MetricsService& MetricsService::instance() {
    static MetricsService service;
    return service;
}

common::Result<RuntimeMetrics> MetricsService::snapshot() const {
    RuntimeMetrics m{};

#ifndef HOST_TEST_BUILD
    m.uptime_s =
        static_cast<std::uint32_t>(static_cast<std::uint64_t>(esp_timer_get_time()) / 1000000ULL);
    m.free_heap_bytes = static_cast<std::uint32_t>(esp_get_free_heap_size());
    m.min_free_heap_bytes = static_cast<std::uint32_t>(esp_get_minimum_free_heap_size());
    m.largest_free_block =
        static_cast<std::uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
#endif

    return common::Result<RuntimeMetrics>::ok(m);
}

} // namespace metrics_service
