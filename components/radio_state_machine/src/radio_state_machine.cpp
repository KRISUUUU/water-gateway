#include "radio_state_machine/radio_state_machine.hpp"

#include "radio_cc1101/radio_cc1101.hpp"

namespace radio_state_machine {

RadioStateMachine& RadioStateMachine::instance() {
    static RadioStateMachine machine;
    return machine;
}

common::Result<void> RadioStateMachine::initialize() {
    if (initialized_) {
        return common::Result<void>(common::ErrorCode::AlreadyInitialized);
    }

    initialized_ = true;
    return common::Result<void>();
}

common::Result<void> RadioStateMachine::start_receiving() {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    return radio_cc1101::RadioCc1101::instance().enter_rx_mode();
}

common::Result<void> RadioStateMachine::recover() {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    return radio_cc1101::RadioCc1101::instance().recover_from_rx_error();
}

}  // namespace radio_state_machine
