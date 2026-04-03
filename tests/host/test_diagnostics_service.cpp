#include "diagnostics_service/diagnostics_service.hpp"
#include "metrics_service/metrics_service.hpp"
#include "rf_diagnostics/rf_diagnostics.hpp"

#include <cassert>
#include <string>

int main() {
    rf_diagnostics::RfDiagnosticsService::instance().clear();
    rf_diagnostics::RfDiagnosticRecord rf_record{};
    rf_record.sequence = 1;
    rf_record.timestamp_epoch_ms = 1234;
    rf_record.monotonic_ms = 5678;
    rf_record.reject_reason = rf_diagnostics::RejectReason::InvalidLength;
    rf_record.orientation = rf_diagnostics::Orientation::BitReversed;
    rf_record.expected_encoded_length = 48;
    rf_record.actual_encoded_length = 42;
    rf_record.captured_prefix_length = 3;
    rf_record.captured_prefix[0] = 0x11;
    rf_record.captured_prefix[1] = 0x22;
    rf_record.captured_prefix[2] = 0x33;
    rf_diagnostics::RfDiagnosticsService::instance().insert(rf_record);

    metrics_service::MetricsService::reset_queue_metrics();
    metrics_service::MetricsService::reset_task_metrics();
    metrics_service::MetricsService::reset_session_metrics();
    metrics_service::MetricsService::report_queue_metrics(4, 15, 16, 100, 7, 2, 5, 6, 12, 80, 9, 3);
    metrics_service::MetricsService::report_task_metrics(250, 75, 50, 10, 2, 3, 4, 5, 6);
    metrics_service::MetricsService::report_session_completed(false, false);
    metrics_service::MetricsService::report_session_completed(true, true);
    metrics_service::MetricsService::report_telegram_validated();
    metrics_service::MetricsService::report_telegram_link_rejected();
    metrics_service::MetricsService::report_session_aborted();

    auto snap_res = diagnostics_service::DiagnosticsService::instance().snapshot();
    assert(!snap_res.is_error());
    const auto snap = snap_res.value();

    // Host build has no NTP sync and uses monotonic fallback visibility fields.
    assert(!snap.ntp.synchronized);
    assert(snap.now_epoch_ms == 0);
    assert(snap.timestamp_uses_monotonic_fallback);
    assert(snap.rf_diagnostics.count == 1);
    assert(snap.rf_diagnostics.records[0].sequence == 1);

    const std::string json = diagnostics_service::DiagnosticsService::to_json(snap);
    assert(json.find("\"time\"") != std::string::npos);
    assert(json.find("\"timestamp_source\":\"monotonic\"") != std::string::npos);
    assert(json.find("\"reset_reason\"") != std::string::npos);
    assert(json.find("\"radio_rx_mode\":\"session_engine\"") != std::string::npos);
    assert(json.find("\"radio_rx_interrupt_path_active\":true") != std::string::npos);
    assert(json.find("\"radio_rx_hardware_validation\"") != std::string::npos);
    assert(json.find("\"rf_diagnostics\"") != std::string::npos);
    assert(json.find("\"recent_sessions\"") != std::string::npos);
    assert(json.find("\"reject_reason\":\"invalid_length\"") != std::string::npos);
    assert(json.find("\"orientation\":\"bit_reversed\"") != std::string::npos);
    assert(json.find("\"captured_prefix_hex\":\"112233\"") != std::string::npos);
    assert(json.find("\"radio_stack_hwm_words\"") != std::string::npos);
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
    assert(json.find("\"recovery_attempts\"") != std::string::npos);
    assert(json.find("\"recovery_failures\"") != std::string::npos);
    assert(json.find("\"soft_failure_streak\"") != std::string::npos);
    assert(json.find("\"consecutive_errors\"") != std::string::npos);
    assert(json.find("\"last_recovery_reason\"") != std::string::npos);
    assert(json.find("\"telegrams_validated\":1") != std::string::npos);
    assert(json.find("\"telegrams_rejected\":1") != std::string::npos);
    assert(json.find("\"sessions_aborted\":1") != std::string::npos);
    assert(json.find("\"radio_crc_available_sessions\":1") != std::string::npos);
    assert(json.find("\"radio_crc_unavailable_sessions\":1") != std::string::npos);
    assert(json.find("\"radio_crc_ok_sessions\":1") != std::string::npos);
    assert(json.find("\"radio_crc_fail_sessions\":0") != std::string::npos);
    assert(json.find("\"enqueue_drop\":7") != std::string::npos);
    assert(json.find("\"enqueue_drop\":9") != std::string::npos);
    assert(json.find("\"peak_depth\":16") != std::string::npos);
    assert(json.find("\"peak_depth\":12") != std::string::npos);
    return 0;
}
