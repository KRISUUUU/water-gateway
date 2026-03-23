#include "health_monitor/health_monitor.hpp"

namespace health_monitor {

HealthMonitor& HealthMonitor::instance() {
    static HealthMonitor monitor;
    return monitor;
}

void HealthMonitor::mark_starting() {
    snapshot_.state = HealthState::Starting;
    snapshot_.summary = "System starting";
}

void HealthMonitor::mark_healthy(const std::string& summary) {
    snapshot_.state = HealthState::Healthy;
    snapshot_.summary = summary;
}

void HealthMonitor::mark_warning(const std::string& summary) {
    snapshot_.state = HealthState::Degraded;
    snapshot_.warning_count += 1;
    snapshot_.summary = summary;
}

void HealthMonitor::mark_error(const std::string& summary) {
    snapshot_.state = HealthState::Error;
    snapshot_.error_count += 1;
    snapshot_.summary = summary;
}

HealthSnapshot HealthMonitor::snapshot() const {
    return snapshot_;
}

}  // namespace health_monitor
