#pragma once

#include "common/error.hpp"
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

    // Notify state machine about read-loop outcomes so repeated soft failures
    // can be escalated to explicit recovery attempts.
    void on_read_success();
    void on_read_failure(common::ErrorCode error);

    RsmState state() const {
        return state_;
    }
    uint32_t consecutive_errors() const {
        return consecutive_errors_;
    }
    uint32_t soft_failure_streak() const {
        return soft_failure_streak_;
    }
    uint32_t recovery_attempts() const {
        return recovery_attempts_;
    }
    uint32_t recovery_failures() const {
        return recovery_failures_;
    }
    common::ErrorCode last_recovery_reason() const {
        return last_recovery_reason_;
    }

  private:
    RadioStateMachine() = default;

    void transition_to(RsmState new_state);
    bool is_escalating_soft_failure(common::ErrorCode error) const;
    void publish_radio_error(common::ErrorCode error);

    bool initialized_ = false;
    RsmState state_ = RsmState::Uninitialized;
    uint32_t consecutive_errors_ = 0;
    uint32_t soft_failure_streak_ = 0;
    uint32_t recovery_attempts_ = 0;
    uint32_t recovery_failures_ = 0;
    common::ErrorCode last_recovery_reason_ = common::ErrorCode::OK;
    bool auto_recovery_ = true;

    static constexpr uint32_t kMaxConsecutiveErrors = 5;
    static constexpr uint32_t kSoftFailureEscalationThreshold = 6;
};

} // namespace radio_state_machine
