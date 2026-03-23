#include <cassert>

#include "health_monitor/health_monitor.hpp"

int main() {
    auto& monitor = health_monitor::HealthMonitor::instance();

    monitor.mark_starting();
    assert(monitor.snapshot().state == health_monitor::HealthState::Starting);

    monitor.mark_warning("warning");
    assert(monitor.snapshot().state == health_monitor::HealthState::Degraded);
    assert(monitor.snapshot().warning_count == 1);

    monitor.mark_error("error");
    assert(monitor.snapshot().state == health_monitor::HealthState::Error);
    assert(monitor.snapshot().error_count == 1);

    return 0;
}
