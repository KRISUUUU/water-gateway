#pragma once

#include "common/result.hpp"
#include <cstdint>

namespace metrics_service {

struct RuntimeQueueMetrics {
    std::uint32_t frame_queue_depth{0};
    // Legacy compatibility field. Mirrors frame_queue_max_depth.
    std::uint32_t frame_queue_peak_depth{0};
    std::uint32_t frame_queue_max_depth{0};
    std::uint32_t frame_enqueue_success{0};
    std::uint32_t frame_enqueue_drop{0};
    std::uint32_t frame_enqueue_errors{0};
    std::uint32_t frame_queue_send_failures{0};

    std::uint32_t mqtt_outbox_depth{0};
    std::uint32_t mqtt_outbox_peak_depth{0};
    std::uint32_t mqtt_outbox_enqueue_success{0};
    std::uint32_t mqtt_outbox_enqueue_drop{0};
    std::uint32_t mqtt_outbox_enqueue_errors{0};
};

struct RuntimeTaskMetrics {
    std::uint32_t radio_loop_age_ms{0};
    std::uint32_t pipeline_loop_age_ms{0};
    std::uint32_t mqtt_loop_age_ms{0};
    std::uint32_t pipeline_frames_processed{0};
    std::uint32_t radio_stall_count{0};
    std::uint32_t pipeline_stall_count{0};
    std::uint32_t mqtt_stall_count{0};
    std::uint32_t watchdog_register_errors{0};
    std::uint32_t watchdog_feed_errors{0};
    std::uint32_t radio_stack_hwm_words{0};
    std::uint32_t pipeline_stack_hwm_words{0};
    std::uint32_t mqtt_stack_hwm_words{0};
    std::uint32_t health_stack_hwm_words{0};
};

struct RuntimeSessionMetrics {
    std::uint32_t completed{0};
    std::uint32_t crc_ok{0};
    std::uint32_t crc_fail{0};
    std::uint32_t incomplete{0};
    std::uint32_t dropped_too_long{0};
};

/// Heap and uptime figures sampled from ESP-IDF (see snapshot()).
struct RuntimeMetrics {
    std::uint32_t uptime_s{0};
    std::uint32_t free_heap_bytes{0};
    std::uint32_t min_free_heap_bytes{0};
    std::uint32_t largest_free_block{0};
    std::uint32_t free_internal_heap_bytes{0};
    std::uint32_t min_internal_heap_bytes{0};
    std::uint32_t reset_reason_code{0};
    RuntimeQueueMetrics queues{};
    RuntimeTaskMetrics tasks{};
    RuntimeSessionMetrics sessions{};
};

class MetricsService {
  public:
    static MetricsService& instance();

    [[nodiscard]] common::Result<RuntimeMetrics> snapshot() const;

    // Runtime queue instrumentation hooks used by app_core runtime tasks.
    static void report_queue_metrics(std::uint32_t frame_queue_depth,
                                     std::uint32_t frame_queue_peak_depth,
                                     std::uint32_t frame_queue_max_depth,
                                     std::uint32_t frame_enqueue_success,
                                     std::uint32_t frame_enqueue_drop,
                                     std::uint32_t frame_enqueue_errors,
                                     std::uint32_t frame_queue_send_failures,
                                     std::uint32_t mqtt_outbox_depth,
                                     std::uint32_t mqtt_outbox_peak_depth,
                                     std::uint32_t mqtt_outbox_enqueue_success,
                                     std::uint32_t mqtt_outbox_enqueue_drop,
                                     std::uint32_t mqtt_outbox_enqueue_errors);
    static void report_task_metrics(std::uint32_t radio_loop_age_ms,
                                    std::uint32_t pipeline_loop_age_ms,
                                    std::uint32_t mqtt_loop_age_ms,
                                    std::uint32_t pipeline_frames_processed,
                                    std::uint32_t radio_stall_count,
                                    std::uint32_t pipeline_stall_count,
                                    std::uint32_t mqtt_stall_count,
                                    std::uint32_t watchdog_register_errors,
                                    std::uint32_t watchdog_feed_errors);
    static void report_task_stack_metrics(std::uint32_t radio_stack_hwm_words,
                                          std::uint32_t pipeline_stack_hwm_words,
                                          std::uint32_t mqtt_stack_hwm_words,
                                          std::uint32_t health_stack_hwm_words);
    static void reset_queue_metrics();
    static void reset_task_metrics();

    static void report_session_completed(bool crc_ok);
    static void report_session_aborted();
    static void reset_session_metrics();

  private:
    MetricsService() = default;
};

} // namespace metrics_service
