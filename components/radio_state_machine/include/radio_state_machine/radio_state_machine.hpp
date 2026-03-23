#pragma once

#include "common/result.hpp"

namespace radio_state_machine {

class RadioStateMachine {
public:
    static RadioStateMachine& instance();

    common::Result<void> initialize();
    common::Result<void> start_receiving();
    common::Result<void> recover();

private:
    RadioStateMachine() = default;
    bool initialized_{false};
};

}  // namespace radio_state_machine
