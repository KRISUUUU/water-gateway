#include "radio_state_machine/radio_state_machine.hpp"

#include <cassert>

int main() {
    auto& rsm = radio_state_machine::RadioStateMachine::instance();
    radio_cc1101::SpiPins pins{23, 19, 18, 5, 4, 2};
    radio_cc1101::SpiBusConfig bus{};

    auto init = rsm.initialize(pins, bus);
    assert(!init.is_error());
    assert(rsm.state() == radio_state_machine::RsmState::Idle);

    auto start = rsm.start_receiving();
    assert(!start.is_error());
    assert(rsm.state() == radio_state_machine::RsmState::Receiving);

    // Expected no-frame polling should not be treated as a fault.
    const auto attempts_before_not_found = rsm.recovery_attempts();
    rsm.on_read_failure(common::ErrorCode::NotFound);
    assert(rsm.soft_failure_streak() == 0);
    assert(rsm.recovery_attempts() == attempts_before_not_found);

    // Non-escalating fault transitions to error but does not auto-recover.
    const auto attempts_before_hard = rsm.recovery_attempts();
    rsm.on_read_failure(common::ErrorCode::BufferFull);
    assert(rsm.state() == radio_state_machine::RsmState::Error);
    assert(rsm.recovery_attempts() == attempts_before_hard);
    auto recover_from_hard = rsm.recover();
    assert(!recover_from_hard.is_error());
    assert(rsm.state() == radio_state_machine::RsmState::Receiving);

    // Soft-failure streak should reset on read success.
    rsm.on_read_failure(common::ErrorCode::Timeout);
    rsm.on_read_failure(common::ErrorCode::Timeout);
    assert(rsm.soft_failure_streak() == 2);
    rsm.on_read_success();
    assert(rsm.soft_failure_streak() == 0);

    // Quality drops should not trigger recovery or build a soft-failure streak.
    const auto attempts_before_quality = rsm.recovery_attempts();
    rsm.on_read_failure(common::ErrorCode::RadioQualityDrop);
    assert(rsm.soft_failure_streak() == 0);
    assert(rsm.recovery_attempts() == attempts_before_quality);
    assert(rsm.state() == radio_state_machine::RsmState::Receiving);

    // Repeated soft failures should escalate to a recovery attempt.
    const auto attempts_before_soft = rsm.recovery_attempts();
    for (int i = 0; i < 5; ++i) {
        rsm.on_read_failure(common::ErrorCode::Timeout);
    }
    assert(rsm.recovery_attempts() == attempts_before_soft);
    rsm.on_read_failure(common::ErrorCode::Timeout);
    assert(rsm.soft_failure_streak() == 0);
    for (int i = 0; i < 5; ++i) {
        rsm.on_read_failure(common::ErrorCode::Timeout);
    }
    assert(rsm.recovery_attempts() >= (attempts_before_soft + 1));
    assert(rsm.last_recovery_reason() == common::ErrorCode::Timeout);
    assert(rsm.soft_failure_streak() == 5);
    assert(rsm.state() == radio_state_machine::RsmState::Receiving);

    // Hard overflow fault should transition to error and raise consecutive error count.
    const auto prev_errors = rsm.consecutive_errors();
    rsm.on_read_failure(common::ErrorCode::RadioFifoOverflow);
    assert(rsm.state() == radio_state_machine::RsmState::Error);
    assert(rsm.consecutive_errors() >= prev_errors + 1);
    assert(rsm.last_recovery_reason() == common::ErrorCode::RadioFifoOverflow);

    return 0;
}
