#pragma once

#include "common/result.hpp"
#include "health_monitor/health_monitor.hpp"
#include "metrics_service/metrics_service.hpp"
#include "mqtt_service/mqtt_service.hpp"
#include "radio_cc1101/radio_cc1101.hpp"
#include "wifi_manager/wifi_manager.hpp"
#include <string>

namespace diagnostics_service {

/// Point-in-time view of radio, connectivity, runtime metrics, and health.
struct DiagnosticsSnapshot {
    radio_cc1101::RadioState radio_state{radio_cc1101::RadioState::Uninitialized};
    radio_cc1101::RadioCounters radio{};
    mqtt_service::MqttStatus mqtt{};
    wifi_manager::WifiStatus wifi{};
    metrics_service::RuntimeMetrics metrics{};
    health_monitor::HealthSnapshot health{};
};

class DiagnosticsService {
  public:
    static DiagnosticsService& instance();

    [[nodiscard]] common::Result<DiagnosticsSnapshot> snapshot() const;

    [[nodiscard]] static std::string to_json(const DiagnosticsSnapshot& snap);

  private:
    DiagnosticsService() = default;
};

} // namespace diagnostics_service
