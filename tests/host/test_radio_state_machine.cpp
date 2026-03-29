#include "radio_state_machine/radio_state_machine.hpp"

#include <cassert>

int main() {
    auto& rsm = radio_state_machine::RadioStateMachine::instance();
    radio_cc1101::SpiPins pins{23, 19, 18, 5, 4, 2};
    radio_cc1101::SpiBusConfig bus{};

    auto init = rsm.initialize(pins, bus);
    assert(!init.is_error());

    // Expected no-frame polling should not be treated as a fault.
    rsm.on_read_failure(common::ErrorCode::NotFound);
    assert(rsm.soft_failure_streak() == 0);

    // Repeated soft failures should escalate to a recovery attempt.
    for (int i = 0; i < 6; ++i) {
        rsm.on_read_failure(common::ErrorCode::Timeout);
    }
    assert(rsm.recovery_attempts() >= 1);
    assert(rsm.last_recovery_reason() == common::ErrorCode::Timeout);
    assert(rsm.soft_failure_streak() == 0);
    assert(rsm.state() == radio_state_machine::RsmState::Receiving);

    // Hard overflow fault should transition to error and raise consecutive error count.
    const auto prev_errors = rsm.consecutive_errors();
    rsm.on_read_failure(common::ErrorCode::RadioFifoOverflow);
    assert(rsm.state() == radio_state_machine::RsmState::Error);
    assert(rsm.consecutive_errors() >= prev_errors + 1);
    assert(rsm.last_recovery_reason() == common::ErrorCode::RadioFifoOverflow);

    return 0;
}
