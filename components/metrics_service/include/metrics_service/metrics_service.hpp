#pragma once

#include "common/result.hpp"
#include <cstdint>

namespace metrics_service {

struct RuntimeQueueMetrics {
    std::uint32_t frame_queue_depth{0};
    std::uint32_t frame_queue_peak_depth{0};
    std::uint32_t frame_enqueue_success{0};
    std::uint32_t frame_enqueue_drop{0};
    std::uint32_t frame_enqueue_errors{0};

    std::uint32_t mqtt_outbox_depth{0};
    std::uint32_t mqtt_outbox_peak_depth{0};
    std::uint32_t mqtt_outbox_enqueue_success{0};
    std::uint32_t mqtt_outbox_enqueue_drop{0};
    std::uint32_t mqtt_outbox_enqueue_errors{0};
};

/// Heap and uptime figures sampled from ESP-IDF (see snapshot()).
struct RuntimeMetrics {
    std::uint32_t uptime_s{0};
    std::uint32_t free_heap_bytes{0};
    std::uint32_t min_free_heap_bytes{0};
    std::uint32_t largest_free_block{0};
    RuntimeQueueMetrics queues{};
};

class MetricsService {
  public:
    static MetricsService& instance();

    [[nodiscard]] common::Result<RuntimeMetrics> snapshot() const;

    // Runtime queue instrumentation hooks used by app_core runtime tasks.
    static void report_queue_metrics(std::uint32_t frame_queue_depth,
                                     std::uint32_t frame_queue_peak_depth,
                                     std::uint32_t frame_enqueue_success,
                                     std::uint32_t frame_enqueue_drop,
                                     std::uint32_t frame_enqueue_errors,
                                     std::uint32_t mqtt_outbox_depth,
                                     std::uint32_t mqtt_outbox_peak_depth,
                                     std::uint32_t mqtt_outbox_enqueue_success,
                                     std::uint32_t mqtt_outbox_enqueue_drop,
                                     std::uint32_t mqtt_outbox_enqueue_errors);
    static void reset_queue_metrics();

  private:
    MetricsService() = default;
};

} // namespace metrics_service
