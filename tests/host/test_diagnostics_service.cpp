#include "diagnostics_service/diagnostics_service.hpp"
#include "metrics_service/metrics_service.hpp"

#include <cassert>
#include <string>

int main() {
    metrics_service::MetricsService::reset_queue_metrics();
    metrics_service::MetricsService::report_queue_metrics(4, 15, 100, 7, 2, 6, 12, 80, 9, 3);

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
    assert(json.find("\"radio_stack_hwm_words\"") != std::string::npos);
    assert(json.find("\"frame_queue\"") != std::string::npos);
    assert(json.find("\"mqtt_outbox\"") != std::string::npos);
    assert(json.find("\"enqueue_drop\":7") != std::string::npos);
    assert(json.find("\"enqueue_drop\":9") != std::string::npos);
    assert(json.find("\"peak_depth\":15") != std::string::npos);
    assert(json.find("\"peak_depth\":12") != std::string::npos);
    return 0;
}
