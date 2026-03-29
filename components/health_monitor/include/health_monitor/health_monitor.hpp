#pragma once

#include "common/result.hpp"
#include <cstdint>
#include <mutex>
#include <string>

namespace health_monitor {

enum class HealthState : std::uint8_t {
    Starting = 0,
    Healthy,
    Warning,
    Error,
};

struct HealthSnapshot {
    HealthState state{HealthState::Starting};
    std::uint32_t warning_count{0};
    std::uint32_t error_count{0};
    std::string last_warning_msg{};
    std::string last_error_msg{};
    std::uint64_t uptime_s{0};
    std::uint64_t last_transition_uptime_s{0};
    std::uint64_t last_warning_uptime_s{0};
    std::uint64_t last_error_uptime_s{0};
};

class HealthMonitor {
  public:
    static HealthMonitor& instance();

    common::Result<void> report_healthy();
    common::Result<void> report_warning(const char* msg);
    common::Result<void> report_error(const char* msg);

    [[nodiscard]] common::Result<HealthSnapshot> snapshot() const;

    [[nodiscard]] static const char* state_to_string(HealthState state);

  private:
    HealthMonitor() = default;

    mutable std::mutex mutex_{};
    HealthSnapshot snapshot_{};
};

} // namespace health_monitor
