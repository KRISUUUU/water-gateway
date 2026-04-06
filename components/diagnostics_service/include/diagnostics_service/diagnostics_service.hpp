#pragma once

#include "common/result.hpp"
#include "health_monitor/health_monitor.hpp"
#include "metrics_service/metrics_service.hpp"
#include "mqtt_service/mqtt_service.hpp"
#include "ntp_service/ntp_service.hpp"
#include "radio_cc1101/radio_cc1101.hpp"
#include "rf_diagnostics/rf_diagnostics.hpp"
#include "wifi_manager/wifi_manager.hpp"
#include <memory>
#include <mutex>
#include <string>

namespace diagnostics_service {

/// Point-in-time view of radio, connectivity, runtime metrics, and health.
struct DiagnosticsSnapshot {
    radio_cc1101::RadioState radio_state{radio_cc1101::RadioState::Uninitialized};
    radio_cc1101::RadioCounters radio{};
    bool radio_rx_polling_mode{true};
    bool radio_rx_interrupt_path_active{false};
    std::uint32_t radio_recovery_attempts{0};
    std::uint32_t radio_recovery_failures{0};
    std::uint32_t radio_soft_failure_streak{0};
    std::uint32_t radio_consecutive_errors{0};
    std::int32_t radio_last_recovery_reason_code{0};
    mqtt_service::MqttStatus mqtt{};
    wifi_manager::WifiStatus wifi{};
    ntp_service::NtpStatus ntp{};
    int64_t now_epoch_ms{0};
    int64_t monotonic_ms{0};
    bool timestamp_uses_monotonic_fallback{false};
    rf_diagnostics::RfDiagnosticsSnapshot rf_diagnostics{};
    metrics_service::RuntimeMetrics metrics{};
    health_monitor::HealthSnapshot health{};
};

class DiagnosticsService {
  public:
    static DiagnosticsService& instance();

    [[nodiscard]] common::Result<DiagnosticsSnapshot> snapshot() const;
    [[nodiscard]] common::Result<std::unique_ptr<DiagnosticsSnapshot>> snapshot_allocated() const;

    [[nodiscard]] static std::string to_json(const DiagnosticsSnapshot& snap);

  private:
    DiagnosticsService() = default;


};

} // namespace diagnostics_service
