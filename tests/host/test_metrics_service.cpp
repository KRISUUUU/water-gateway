#include "metrics_service/metrics_service.hpp"

#include <cassert>
#include <cstdint>

int main() {
    metrics_service::MetricsService::reset_queue_metrics();
    metrics_service::MetricsService::reset_task_metrics();

    metrics_service::MetricsService::report_queue_metrics(3, 7, 11, 100, 4, 4, 2, 2, 9, 55, 6, 6);
    metrics_service::MetricsService::report_task_metrics(120, 80, 60, 1234, 2, 1, 3, 1, 5);
    metrics_service::MetricsService::report_task_stack_metrics(512, 768, 1024, 640);

    auto snap_res = metrics_service::MetricsService::instance().snapshot();
    assert(!snap_res.is_error());
    const auto snap = snap_res.value();

    assert(snap.queues.frame_queue_depth == 3);
    assert(snap.queues.frame_queue_peak_depth == 11);
    assert(snap.queues.frame_queue_max_depth == 11);
    assert(snap.queues.frame_enqueue_success == 100);
    assert(snap.queues.frame_enqueue_drop == 4);
    assert(snap.queues.frame_enqueue_errors == 4);
    assert(snap.queues.frame_queue_send_failures == 2);
    assert(snap.queues.mqtt_outbox_depth == 2);
    assert(snap.queues.mqtt_outbox_peak_depth == 9);
    assert(snap.queues.mqtt_outbox_enqueue_success == 55);
    assert(snap.queues.mqtt_outbox_enqueue_drop == 6);
    assert(snap.queues.mqtt_outbox_enqueue_errors == 6);

    assert(snap.tasks.radio_loop_age_ms == 120);
    assert(snap.tasks.pipeline_loop_age_ms == 80);
    assert(snap.tasks.mqtt_loop_age_ms == 60);
    assert(snap.tasks.pipeline_frames_processed == 1234);
    assert(snap.tasks.radio_stall_count == 2);
    assert(snap.tasks.pipeline_stall_count == 1);
    assert(snap.tasks.mqtt_stall_count == 3);
    assert(snap.tasks.watchdog_register_errors == 1);
    assert(snap.tasks.watchdog_feed_errors == 5);
    assert(snap.tasks.radio_stack_hwm_words == 512);
    assert(snap.tasks.pipeline_stack_hwm_words == 768);
    assert(snap.tasks.mqtt_stack_hwm_words == 1024);
    assert(snap.tasks.health_stack_hwm_words == 640);
    assert(snap.free_internal_heap_bytes == 0);
    assert(snap.min_internal_heap_bytes == 0);
    assert(snap.reset_reason_code == 0);

    metrics_service::MetricsService::report_queue_metrics(
        UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX,
        UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX);
    snap_res = metrics_service::MetricsService::instance().snapshot();
    assert(!snap_res.is_error());
    const auto saturated_snap = snap_res.value();
    assert(saturated_snap.queues.frame_queue_depth == UINT32_MAX);
    assert(saturated_snap.queues.frame_enqueue_drop == UINT32_MAX);
    assert(saturated_snap.queues.frame_queue_max_depth == UINT32_MAX);
    assert(saturated_snap.queues.frame_queue_send_failures == UINT32_MAX);
    assert(saturated_snap.queues.mqtt_outbox_peak_depth == UINT32_MAX);
    assert(saturated_snap.queues.mqtt_outbox_enqueue_errors == UINT32_MAX);

    metrics_service::MetricsService::reset_queue_metrics();
    metrics_service::MetricsService::reset_task_metrics();
    snap_res = metrics_service::MetricsService::instance().snapshot();
    assert(!snap_res.is_error());
    const auto reset_snap = snap_res.value();
    assert(reset_snap.queues.frame_queue_depth == 0);
    assert(reset_snap.queues.mqtt_outbox_depth == 0);
    assert(reset_snap.tasks.radio_loop_age_ms == 0);
    assert(reset_snap.tasks.watchdog_feed_errors == 0);
    assert(reset_snap.tasks.radio_stack_hwm_words == 0);
    assert(reset_snap.tasks.pipeline_stack_hwm_words == 0);
    assert(reset_snap.tasks.mqtt_stack_hwm_words == 0);
    assert(reset_snap.tasks.health_stack_hwm_words == 0);

    return 0;
}
