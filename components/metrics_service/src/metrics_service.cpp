#include "metrics_service/metrics_service.hpp"

#include <atomic>

#ifndef HOST_TEST_BUILD
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#endif

namespace metrics_service {

namespace {

std::atomic<std::uint32_t> g_frame_queue_depth{0};
std::atomic<std::uint32_t> g_frame_queue_peak_depth{0};
std::atomic<std::uint32_t> g_frame_enqueue_success{0};
std::atomic<std::uint32_t> g_frame_enqueue_drop{0};
std::atomic<std::uint32_t> g_frame_enqueue_errors{0};

std::atomic<std::uint32_t> g_mqtt_outbox_depth{0};
std::atomic<std::uint32_t> g_mqtt_outbox_peak_depth{0};
std::atomic<std::uint32_t> g_mqtt_outbox_enqueue_success{0};
std::atomic<std::uint32_t> g_mqtt_outbox_enqueue_drop{0};
std::atomic<std::uint32_t> g_mqtt_outbox_enqueue_errors{0};

std::atomic<std::uint32_t> g_radio_loop_age_ms{0};
std::atomic<std::uint32_t> g_pipeline_loop_age_ms{0};
std::atomic<std::uint32_t> g_mqtt_loop_age_ms{0};
std::atomic<std::uint32_t> g_pipeline_frames_processed{0};
std::atomic<std::uint32_t> g_radio_stall_count{0};
std::atomic<std::uint32_t> g_pipeline_stall_count{0};
std::atomic<std::uint32_t> g_mqtt_stall_count{0};
std::atomic<std::uint32_t> g_watchdog_register_errors{0};
std::atomic<std::uint32_t> g_watchdog_feed_errors{0};

} // namespace

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

    m.queues.frame_queue_depth = g_frame_queue_depth.load(std::memory_order_relaxed);
    m.queues.frame_queue_peak_depth = g_frame_queue_peak_depth.load(std::memory_order_relaxed);
    m.queues.frame_enqueue_success = g_frame_enqueue_success.load(std::memory_order_relaxed);
    m.queues.frame_enqueue_drop = g_frame_enqueue_drop.load(std::memory_order_relaxed);
    m.queues.frame_enqueue_errors = g_frame_enqueue_errors.load(std::memory_order_relaxed);

    m.queues.mqtt_outbox_depth = g_mqtt_outbox_depth.load(std::memory_order_relaxed);
    m.queues.mqtt_outbox_peak_depth = g_mqtt_outbox_peak_depth.load(std::memory_order_relaxed);
    m.queues.mqtt_outbox_enqueue_success =
        g_mqtt_outbox_enqueue_success.load(std::memory_order_relaxed);
    m.queues.mqtt_outbox_enqueue_drop = g_mqtt_outbox_enqueue_drop.load(std::memory_order_relaxed);
    m.queues.mqtt_outbox_enqueue_errors =
        g_mqtt_outbox_enqueue_errors.load(std::memory_order_relaxed);

    m.tasks.radio_loop_age_ms = g_radio_loop_age_ms.load(std::memory_order_relaxed);
    m.tasks.pipeline_loop_age_ms = g_pipeline_loop_age_ms.load(std::memory_order_relaxed);
    m.tasks.mqtt_loop_age_ms = g_mqtt_loop_age_ms.load(std::memory_order_relaxed);
    m.tasks.pipeline_frames_processed = g_pipeline_frames_processed.load(std::memory_order_relaxed);
    m.tasks.radio_stall_count = g_radio_stall_count.load(std::memory_order_relaxed);
    m.tasks.pipeline_stall_count = g_pipeline_stall_count.load(std::memory_order_relaxed);
    m.tasks.mqtt_stall_count = g_mqtt_stall_count.load(std::memory_order_relaxed);
    m.tasks.watchdog_register_errors = g_watchdog_register_errors.load(std::memory_order_relaxed);
    m.tasks.watchdog_feed_errors = g_watchdog_feed_errors.load(std::memory_order_relaxed);

    return common::Result<RuntimeMetrics>::ok(m);
}

void MetricsService::report_queue_metrics(std::uint32_t frame_queue_depth,
                                          std::uint32_t frame_queue_peak_depth,
                                          std::uint32_t frame_enqueue_success,
                                          std::uint32_t frame_enqueue_drop,
                                          std::uint32_t frame_enqueue_errors,
                                          std::uint32_t mqtt_outbox_depth,
                                          std::uint32_t mqtt_outbox_peak_depth,
                                          std::uint32_t mqtt_outbox_enqueue_success,
                                          std::uint32_t mqtt_outbox_enqueue_drop,
                                          std::uint32_t mqtt_outbox_enqueue_errors) {
    g_frame_queue_depth.store(frame_queue_depth, std::memory_order_relaxed);
    g_frame_queue_peak_depth.store(frame_queue_peak_depth, std::memory_order_relaxed);
    g_frame_enqueue_success.store(frame_enqueue_success, std::memory_order_relaxed);
    g_frame_enqueue_drop.store(frame_enqueue_drop, std::memory_order_relaxed);
    g_frame_enqueue_errors.store(frame_enqueue_errors, std::memory_order_relaxed);

    g_mqtt_outbox_depth.store(mqtt_outbox_depth, std::memory_order_relaxed);
    g_mqtt_outbox_peak_depth.store(mqtt_outbox_peak_depth, std::memory_order_relaxed);
    g_mqtt_outbox_enqueue_success.store(mqtt_outbox_enqueue_success, std::memory_order_relaxed);
    g_mqtt_outbox_enqueue_drop.store(mqtt_outbox_enqueue_drop, std::memory_order_relaxed);
    g_mqtt_outbox_enqueue_errors.store(mqtt_outbox_enqueue_errors, std::memory_order_relaxed);
}

void MetricsService::reset_queue_metrics() {
    report_queue_metrics(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

void MetricsService::report_task_metrics(std::uint32_t radio_loop_age_ms,
                                         std::uint32_t pipeline_loop_age_ms,
                                         std::uint32_t mqtt_loop_age_ms,
                                         std::uint32_t pipeline_frames_processed,
                                         std::uint32_t radio_stall_count,
                                         std::uint32_t pipeline_stall_count,
                                         std::uint32_t mqtt_stall_count,
                                         std::uint32_t watchdog_register_errors,
                                         std::uint32_t watchdog_feed_errors) {
    g_radio_loop_age_ms.store(radio_loop_age_ms, std::memory_order_relaxed);
    g_pipeline_loop_age_ms.store(pipeline_loop_age_ms, std::memory_order_relaxed);
    g_mqtt_loop_age_ms.store(mqtt_loop_age_ms, std::memory_order_relaxed);
    g_pipeline_frames_processed.store(pipeline_frames_processed, std::memory_order_relaxed);
    g_radio_stall_count.store(radio_stall_count, std::memory_order_relaxed);
    g_pipeline_stall_count.store(pipeline_stall_count, std::memory_order_relaxed);
    g_mqtt_stall_count.store(mqtt_stall_count, std::memory_order_relaxed);
    g_watchdog_register_errors.store(watchdog_register_errors, std::memory_order_relaxed);
    g_watchdog_feed_errors.store(watchdog_feed_errors, std::memory_order_relaxed);
}

void MetricsService::reset_task_metrics() {
    report_task_metrics(0, 0, 0, 0, 0, 0, 0, 0, 0);
}

} // namespace metrics_service
