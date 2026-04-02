#include "host_test_stubs.hpp"
#include "radio_cc1101/radio_cc1101.hpp"
#include <cassert>
#include <cstdio>

using namespace radio_cc1101;

static void test_timeout_ends_burst_even_when_fifo_is_not_empty() {
    const auto action = raw_burst_timeout_action(20, 33, 20);
    assert(action == RawBurstTimeoutAction::EndBurst);
    printf("  PASS: timeout ends burst with continuous FIFO data\n");
}

static void test_timeout_without_bytes_returns_not_found() {
    const auto action = raw_burst_timeout_action(20, 0, 20);
    assert(action == RawBurstTimeoutAction::ReturnNotFound);
    printf("  PASS: timeout without bytes returns not found\n");
}

static void test_sub_timeout_continues_capture() {
    const auto action = raw_burst_timeout_action(19, 64, 20);
    assert(action == RawBurstTimeoutAction::Continue);
    printf("  PASS: sub-timeout capture continues\n");
}

int main() {
    printf("=== test_radio_cc1101 ===\n");
    test_timeout_ends_burst_even_when_fifo_is_not_empty();
    test_timeout_without_bytes_returns_not_found();
    test_sub_timeout_continues_capture();
    printf("All radio_cc1101 tests passed.\n");
    return 0;
}
