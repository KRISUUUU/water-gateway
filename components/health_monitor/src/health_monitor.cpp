#include "health_monitor/health_monitor.hpp"

#ifndef HOST_TEST_BUILD
#include "esp_timer.h"
#endif

namespace health_monitor {

HealthMonitor& HealthMonitor::instance() {
    static HealthMonitor monitor;
    return monitor;
}

common::Result<void> HealthMonitor::report_healthy() {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.state = HealthState::Healthy;
    return common::Result<void>::ok();
}

common::Result<void> HealthMonitor::report_warning(const char* msg) {
    if (!msg) {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.warning_count += 1;
    snapshot_.last_warning_msg = msg;
    if (snapshot_.state != HealthState::Error) {
        snapshot_.state = HealthState::Warning;
    }
    return common::Result<void>::ok();
}

common::Result<void> HealthMonitor::report_error(const char* msg) {
    if (!msg) {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.error_count += 1;
    snapshot_.last_error_msg = msg;
    snapshot_.state = HealthState::Error;
    return common::Result<void>::ok();
}

common::Result<HealthSnapshot> HealthMonitor::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    HealthSnapshot s = snapshot_;
#ifndef HOST_TEST_BUILD
    s.uptime_s =
        static_cast<std::uint64_t>(esp_timer_get_time() / 1000000ULL);
#else
    s.uptime_s = 0;
#endif
    return common::Result<HealthSnapshot>::ok(s);
}

const char* HealthMonitor::state_to_string(HealthState state) {
    switch (state) {
    case HealthState::Starting:
        return "Starting";
    case HealthState::Healthy:
        return "Healthy";
    case HealthState::Warning:
        return "Warning";
    case HealthState::Error:
        return "Error";
    }
    return "Unknown";
}

} // namespace health_monitor
