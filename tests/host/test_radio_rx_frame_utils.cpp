#include "radio_cc1101/radio_cc1101.hpp"
#include "radio_cc1101/rx_frame_utils.hpp"

#include <cassert>
#include <cstdio>

using namespace radio_cc1101;

static void test_plan_rx_frame_drain_valid() {
    auto plan = plan_rx_frame_drain(0x19, RawRadioFrame::MAX_DATA_SIZE);
    assert(plan.is_ok());
    assert(plan.value().frame_length == 26U);
    assert(plan.value().bytes_after_length == 27U);
    printf("  PASS: plan_rx_frame_drain valid\n");
}

static void test_plan_rx_frame_drain_rejects_zero() {
    auto plan = plan_rx_frame_drain(0x00, RawRadioFrame::MAX_DATA_SIZE);
    assert(plan.is_error());
    assert(plan.error() == common::ErrorCode::InvalidArgument);
    printf("  PASS: plan_rx_frame_drain rejects zero\n");
}

static void test_plan_rx_frame_drain_rejects_oversize() {
    auto plan = plan_rx_frame_drain(32U, 32U);
    assert(plan.is_error());
    assert(plan.error() == common::ErrorCode::InvalidArgument);
    printf("  PASS: plan_rx_frame_drain rejects oversize\n");
}

static void test_next_rx_burst_size_clamps_to_available_and_max() {
    assert(next_rx_burst_size(18U, 64U, 64U) == 18U);
    assert(next_rx_burst_size(40U, 8U, 64U) == 8U);
    assert(next_rx_burst_size(40U, 32U, 16U) == 16U);
    printf("  PASS: next_rx_burst_size clamps\n");
}

int main() {
    printf("=== test_radio_rx_frame_utils ===\n");
    test_plan_rx_frame_drain_valid();
    test_plan_rx_frame_drain_rejects_zero();
    test_plan_rx_frame_drain_rejects_oversize();
    test_next_rx_burst_size_clamps_to_available_and_max();
    printf("All radio RX frame utility tests passed.\n");
    return 0;
}
