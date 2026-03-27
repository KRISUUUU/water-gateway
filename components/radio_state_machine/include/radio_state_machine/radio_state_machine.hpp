#pragma once

#include "common/result.hpp"
#include "radio_cc1101/radio_cc1101.hpp"
#include <cstdint>

namespace radio_state_machine {

enum class RsmState : uint8_t {
    Uninitialized = 0,
    Initializing,
    Receiving,
    Error,
    Recovering,
};

class RadioStateMachine {
  public:
    static RadioStateMachine& instance();

    // Initialize radio hardware with pin config
    common::Result<void> initialize(const radio_cc1101::SpiPins& pins);
    common::Result<void> initialize(const radio_cc1101::SpiPins& pins,
                                    const radio_cc1101::SpiBusConfig& bus_config);

    // Enter RX mode
    common::Result<void> start_receiving();

    // Attempt recovery from error state
    common::Result<void> recover();

    // Called periodically from the radio task to check for errors
    // and handle automatic recovery
    void tick();

    RsmState state() const {
        return state_;
    }
    uint32_t consecutive_errors() const {
        return consecutive_errors_;
    }

  private:
    RadioStateMachine() = default;

    void transition_to(RsmState new_state);

    bool initialized_ = false;
    RsmState state_ = RsmState::Uninitialized;
    uint32_t consecutive_errors_ = 0;
    bool auto_recovery_ = true;
    // R1 fix: log max-errors message only once to prevent log storm (~500 msg/s)
    bool logged_max_errors_ = false;

    static constexpr uint32_t kMaxConsecutiveErrors = 5;
};

} // namespace radio_state_machine
