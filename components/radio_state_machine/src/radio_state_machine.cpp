#include "radio_state_machine/radio_state_machine.hpp"
#include "event_bus/event_bus.hpp"

#ifndef HOST_TEST_BUILD
#include "esp_log.h"
static const char* TAG = "radio_sm";
#endif

namespace radio_state_machine {

RadioStateMachine& RadioStateMachine::instance() {
    static RadioStateMachine sm;
    return sm;
}

common::Result<void> RadioStateMachine::initialize(const radio_cc1101::SpiPins& pins) {
    return initialize(pins, radio_cc1101::SpiBusConfig{});
}

common::Result<void> RadioStateMachine::initialize(const radio_cc1101::SpiPins& pins,
                                                   const radio_cc1101::SpiBusConfig& bus_config) {
    if (initialized_) {
        return common::Result<void>::error(common::ErrorCode::AlreadyInitialized);
    }

    transition_to(RsmState::Initializing);

    auto& radio = radio_cc1101::RadioCc1101::instance();
    auto result = radio.initialize(pins, bus_config);
    if (result.is_error()) {
#ifndef HOST_TEST_BUILD
        ESP_LOGE(TAG, "Radio initialization failed");
#endif
        transition_to(RsmState::Error);
        return result;
    }

    initialized_ = true;
    consecutive_errors_ = 0;
    soft_failure_streak_ = 0;
    recovery_attempts_ = 0;
    recovery_failures_ = 0;
    last_recovery_reason_ = common::ErrorCode::OK;
    transition_to(RsmState::Idle);
    return common::Result<void>::ok();
}

common::Result<void> RadioStateMachine::start_receiving() {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }

    auto& radio = radio_cc1101::RadioCc1101::instance();
    auto result = radio.start_rx();
    if (result.is_error()) {
#ifndef HOST_TEST_BUILD
        ESP_LOGE(TAG, "Failed to enter RX mode");
#endif
        consecutive_errors_++;
        last_recovery_reason_ = result.error();
        transition_to(RsmState::Error);
        publish_radio_error(result.error());
        return result;
    }

    consecutive_errors_ = 0;
    soft_failure_streak_ = 0;
    transition_to(RsmState::Receiving);
    return common::Result<void>::ok();
}

common::Result<void> RadioStateMachine::recover() {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }

    transition_to(RsmState::Recovering);
    recovery_attempts_++;

#ifndef HOST_TEST_BUILD
    ESP_LOGW(TAG, "Attempting radio recovery (reason=%s/%d, hard=%lu, soft=%lu, attempts=%lu)",
             common::error_code_to_string(last_recovery_reason_),
             static_cast<int>(last_recovery_reason_), (unsigned long)consecutive_errors_,
             (unsigned long)soft_failure_streak_, (unsigned long)recovery_attempts_);
#endif

    auto& radio = radio_cc1101::RadioCc1101::instance();
    auto result = radio.recover();
    if (result.is_error()) {
        consecutive_errors_++;
        recovery_failures_++;
#ifndef HOST_TEST_BUILD
        ESP_LOGE(TAG, "Radio recovery failed");
#endif
        transition_to(RsmState::Error);
        return result;
    }

    consecutive_errors_ = 0;
    soft_failure_streak_ = 0;
    transition_to(RsmState::Receiving);

    event_bus::EventBus::instance().publish(event_bus::EventType::RadioRecovered);

    return common::Result<void>::ok();
}

void RadioStateMachine::on_read_success() {
    if (!initialized_) {
        return;
    }
    soft_failure_streak_ = 0;
    consecutive_errors_ = 0;
    if (state_ == RsmState::Error) {
        transition_to(RsmState::Receiving);
    }
}

bool RadioStateMachine::is_escalating_soft_failure(common::ErrorCode error) const {
    return error == common::ErrorCode::Timeout || error == common::ErrorCode::RadioSpiError;
}

void RadioStateMachine::publish_radio_error(common::ErrorCode error) {
    event_bus::EventBus::instance().publish(event_bus::EventType::RadioError,
                                            static_cast<int32_t>(error));
}

void RadioStateMachine::on_read_failure(common::ErrorCode error) {
    if (!initialized_) {
        return;
    }
    if (error == common::ErrorCode::NotFound) {
        // Expected "no frame available" result for the polling RX loop.
        return;
    }

    if (error == common::ErrorCode::RadioQualityDrop) {
        // Framing/boundary quality drops mean RX stayed alive but the captured burst was not
        // usable. They should not drive recovery storms.
        soft_failure_streak_ = 0;
        return;
    }

    if (error == common::ErrorCode::RadioFifoOverflow) {
        consecutive_errors_++;
        soft_failure_streak_ = 0;
        last_recovery_reason_ = error;
        transition_to(RsmState::Error);
        publish_radio_error(error);
        return;
    }

    if (is_escalating_soft_failure(error)) {
        // Soft failures keep default polling behavior unchanged unless they repeat enough times to
        // justify an explicit recovery attempt.
        soft_failure_streak_++;
        last_recovery_reason_ = error;
        if (soft_failure_streak_ >= kSoftFailureEscalationThreshold) {
#ifndef HOST_TEST_BUILD
            ESP_LOGW(TAG,
                     "Escalating repeated soft radio failures (reason=%s/%d, streak=%lu, "
                     "threshold=%lu)",
                     common::error_code_to_string(error), static_cast<int>(error),
                     (unsigned long)soft_failure_streak_,
                     (unsigned long)kSoftFailureEscalationThreshold);
#endif
            transition_to(RsmState::Error);
            publish_radio_error(error);
            recover();
        }
        return;
    }

    // Any other radio error is treated as hard and recovered via normal path.
    consecutive_errors_++;
    soft_failure_streak_ = 0;
    last_recovery_reason_ = error;
    transition_to(RsmState::Error);
    publish_radio_error(error);
}

void RadioStateMachine::tick() {
    if (!initialized_ || !auto_recovery_)
        return;

    auto& radio = radio_cc1101::RadioCc1101::instance();

    if (radio.state() == radio_cc1101::RadioState::Error) {
        if (consecutive_errors_ < kMaxConsecutiveErrors) {
            recover();
        } else {
#ifndef HOST_TEST_BUILD
            ESP_LOGE(TAG,
                     "Max consecutive errors reached (%lu), "
                     "radio recovery disabled until manual intervention",
                     (unsigned long)kMaxConsecutiveErrors);
#endif
            transition_to(RsmState::Error);
        }
    }
}

void RadioStateMachine::transition_to(RsmState new_state) {
    if (state_ == new_state)
        return;

#ifndef HOST_TEST_BUILD
    static const char* state_names[] = {"Uninitialized", "Initializing", "Idle", "Receiving",
                                        "Error", "Recovering"};
    ESP_LOGD(TAG, "State: %s -> %s", state_names[static_cast<int>(state_)],
             state_names[static_cast<int>(new_state)]);
#endif

    state_ = new_state;
}

} // namespace radio_state_machine
