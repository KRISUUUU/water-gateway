#include "diagnostics_service/diagnostics_service.hpp"
#include "metrics_service/metrics_service.hpp"

#include <cassert>
#include <string>

int main() {
    metrics_service::MetricsService::reset_queue_metrics();
    metrics_service::MetricsService::reset_task_metrics();
    metrics_service::MetricsService::report_queue_metrics(4, 15, 16, 100, 7, 2, 5, 6, 12, 80, 9, 3);
    metrics_service::MetricsService::report_task_metrics(250, 2, 75, 50, 10, 20, 3, 4, 5, 6, 70, 2,
                                                         8, 1, 2, 3, 4, 5, 6);

    auto snap_res = diagnostics_service::DiagnosticsService::instance().snapshot();
    assert(!snap_res.is_error());
    const auto snap = snap_res.value();

    // Host build has no NTP sync and uses monotonic fallback visibility fields.
    assert(!snap.ntp.synchronized);
    assert(snap.now_epoch_ms == 0);
    assert(snap.timestamp_uses_monotonic_fallback);

    const std::string json = diagnostics_service::DiagnosticsService::to_json(snap);
    assert(json.find("\"time\"") != std::string::npos);
    assert(json.find("\"timestamp_source\":\"monotonic\"") != std::string::npos);
    assert(json.find("\"reset_reason\"") != std::string::npos);
    assert(json.find("\"radio_rx_mode\":\"polling\"") != std::string::npos);
    assert(json.find("\"radio_rx_interrupt_path_active\":false") != std::string::npos);
    assert(json.find("\"radio_rx_hardware_validation\"") != std::string::npos);
    assert(json.find("\"radio_poll_delay_ms\":2") != std::string::npos);
    assert(json.find("\"radio_stack_hwm_words\"") != std::string::npos);
    assert(json.find("\"radio_read_not_found_count\"") != std::string::npos);
    assert(json.find("\"radio_not_found_streak_peak\"") != std::string::npos);
    assert(json.find("\"radio_poll_iterations\"") != std::string::npos);
    assert(json.find("\"radio_timeout_streak\"") != std::string::npos);
    assert(json.find("\"radio_timeout_streak_peak\"") != std::string::npos);
    assert(json.find("\"frame_queue\"") != std::string::npos);
    assert(json.find("\"mqtt_outbox\"") != std::string::npos);
    assert(json.find("\"frame_queue_max_depth\"") != std::string::npos);
    assert(json.find("\"frame_queue_send_failures\"") != std::string::npos);
    assert(json.find("\"frame_queue_max_depth\":16") != std::string::npos);
    assert(json.find("\"frame_queue_send_failures\":5") != std::string::npos);
    assert(json.find("\"outbox_enqueue_failures\"") != std::string::npos);
    assert(json.find("\"outbox_oversize_rejections\"") != std::string::npos);
    assert(json.find("\"outbox_max_depth\"") != std::string::npos);
    assert(json.find("\"outbox_dropped_disconnected\"") != std::string::npos);
    assert(json.find("\"outbox_carry_pending\"") != std::string::npos);
    assert(json.find("\"outbox_carry_retry_attempts\"") != std::string::npos);
    assert(json.find("\"outbox_carry_drops\"") != std::string::npos);
    assert(json.find("\"rx_read_calls\"") != std::string::npos);
    assert(json.find("\"rx_not_found\"") != std::string::npos);
    assert(json.find("\"rx_timeouts\"") != std::string::npos);
    assert(json.find("\"recovery_attempts\"") != std::string::npos);
    assert(json.find("\"recovery_failures\"") != std::string::npos);
    assert(json.find("\"soft_failure_streak\"") != std::string::npos);
    assert(json.find("\"consecutive_errors\"") != std::string::npos);
    assert(json.find("\"last_recovery_reason\"") != std::string::npos);
    assert(json.find("\"enqueue_drop\":7") != std::string::npos);
    assert(json.find("\"enqueue_drop\":9") != std::string::npos);
    assert(json.find("\"peak_depth\":16") != std::string::npos);
    assert(json.find("\"peak_depth\":12") != std::string::npos);
    return 0;
}
