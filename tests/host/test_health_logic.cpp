#include "host_test_stubs.hpp"
#include "health_monitor/health_monitor.hpp"
#include <cassert>
#include <cstdio>

using namespace health_monitor;

static void reset_health_monitor() {
    HealthMonitor::instance().reset_for_test();
}

static void test_initial_state_is_starting() {
    reset_health_monitor();
    auto& hm = HealthMonitor::instance();
    auto snap = hm.snapshot();
    assert(snap.is_ok());
    assert(snap.value().state == HealthState::Starting);
    printf("  PASS: initial state is Starting\n");
}

static void test_report_healthy() {
    reset_health_monitor();
    auto& hm = HealthMonitor::instance();
    hm.report_healthy();
    auto snap = hm.snapshot();
    assert(snap.is_ok());
    assert(snap.value().state == HealthState::Healthy);
    printf("  PASS: report_healthy transitions to Healthy\n");
}

static void test_report_warning() {
    reset_health_monitor();
    auto& hm = HealthMonitor::instance();
    hm.report_healthy();
    hm.report_warning("WiFi disconnected");
    auto snap = hm.snapshot();
    assert(snap.is_ok());
    assert(snap.value().state == HealthState::Warning);
    assert(snap.value().warning_count >= 1);
    assert(snap.value().last_warning_msg == "WiFi disconnected");
    printf("  PASS: report_warning transitions to Warning\n");
}

static void test_report_error() {
    reset_health_monitor();
    auto& hm = HealthMonitor::instance();
    hm.report_error("Radio failed");
    auto snap = hm.snapshot();
    assert(snap.is_ok());
    assert(snap.value().state == HealthState::Error);
    assert(snap.value().error_count >= 1);
    assert(snap.value().last_error_msg == "Radio failed");
    printf("  PASS: report_error transitions to Error\n");
}

static void test_warning_does_not_override_error() {
    reset_health_monitor();
    auto& hm = HealthMonitor::instance();
    hm.report_error("critical");
    auto before = hm.snapshot();
    assert(before.is_ok());
    const auto warnings_before = before.value().warning_count;
    hm.report_warning("just a warning");
    auto snap = hm.snapshot();
    assert(snap.is_ok());
    // Error should not be downgraded to Warning
    assert(snap.value().state == HealthState::Error);
    assert(snap.value().warning_count == warnings_before + 1);
    assert(snap.value().last_warning_msg == "just a warning");
    printf("  PASS: warning does not override error\n");
}

static void test_healthy_recovers_from_warning() {
    reset_health_monitor();
    auto& hm = HealthMonitor::instance();
    hm.report_warning("temp issue");
    hm.report_healthy();
    auto snap = hm.snapshot();
    assert(snap.is_ok());
    assert(snap.value().state == HealthState::Healthy);
    printf("  PASS: healthy recovers from warning\n");
}

static void test_null_warning_rejected() {
    reset_health_monitor();
    auto& hm = HealthMonitor::instance();
    auto result = hm.report_warning(nullptr);
    assert(result.is_error());
    assert(result.error() == common::ErrorCode::InvalidArgument);
    printf("  PASS: null warning rejected\n");
}

static void test_null_error_rejected() {
    reset_health_monitor();
    auto& hm = HealthMonitor::instance();
    auto result = hm.report_error(nullptr);
    assert(result.is_error());
    assert(result.error() == common::ErrorCode::InvalidArgument);
    printf("  PASS: null error rejected\n");
}

static void test_state_to_string() {
    reset_health_monitor();
    assert(std::string(HealthMonitor::state_to_string(HealthState::Starting)) == "Starting");
    assert(std::string(HealthMonitor::state_to_string(HealthState::Healthy)) == "Healthy");
    assert(std::string(HealthMonitor::state_to_string(HealthState::Warning)) == "Warning");
    assert(std::string(HealthMonitor::state_to_string(HealthState::Error)) == "Error");
    printf("  PASS: state_to_string\n");
}

int main() {
    printf("=== test_health_logic ===\n");
    test_initial_state_is_starting();
    test_report_healthy();
    test_report_warning();
    test_report_error();
    test_warning_does_not_override_error();
    test_healthy_recovers_from_warning();
    test_null_warning_rejected();
    test_null_error_rejected();
    test_state_to_string();
    printf("All health logic tests passed.\n");
    return 0;
}
