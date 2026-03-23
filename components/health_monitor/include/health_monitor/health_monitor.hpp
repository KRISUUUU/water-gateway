#pragma once

#include <cstdint>
#include <string>

namespace health_monitor {

enum class HealthState {
    Unknown = 0,
    Starting,
    Healthy,
    Degraded,
    Error
};

struct HealthSnapshot {
    HealthState state{HealthState::Unknown};
    std::uint32_t warning_count{0};
    std::uint32_t error_count{0};
    std::string summary{};
};

class HealthMonitor {
public:
    static HealthMonitor& instance();

    void mark_starting();
    void mark_healthy(const std::string& summary);
    void mark_warning(const std::string& summary);
    void mark_error(const std::string& summary);

    [[nodiscard]] HealthSnapshot snapshot() const;

private:
    HealthMonitor() = default;

    HealthSnapshot snapshot_{};
};

}  // namespace health_monitor
