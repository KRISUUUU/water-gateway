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

common::Result<void> RadioStateMachine::initialize(
    const radio_cc1101::SpiPins& pins) {
    if (initialized_) {
        return common::Result<void>::error(common::ErrorCode::AlreadyInitialized);
    }

    transition_to(RsmState::Initializing);

    auto& radio = radio_cc1101::RadioCc1101::instance();
    auto result = radio.initialize(pins);
    if (result.is_error()) {
#ifndef HOST_TEST_BUILD
        ESP_LOGE(TAG, "Radio initialization failed");
#endif
        transition_to(RsmState::Error);
        return result;
    }

    initialized_ = true;
    transition_to(RsmState::Receiving);
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
        transition_to(RsmState::Error);
        event_bus::EventBus::instance().publish(
            event_bus::EventType::RadioError,
            static_cast<int32_t>(result.error()));
        return result;
    }

    consecutive_errors_ = 0;
    transition_to(RsmState::Receiving);
    return common::Result<void>::ok();
}

common::Result<void> RadioStateMachine::recover() {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }

    transition_to(RsmState::Recovering);

#ifndef HOST_TEST_BUILD
    ESP_LOGW(TAG, "Attempting radio recovery (error count: %lu)",
             (unsigned long)consecutive_errors_);
#endif

    auto& radio = radio_cc1101::RadioCc1101::instance();
    auto result = radio.recover();
    if (result.is_error()) {
        consecutive_errors_++;
#ifndef HOST_TEST_BUILD
        ESP_LOGE(TAG, "Radio recovery failed");
#endif
        transition_to(RsmState::Error);
        return result;
    }

    consecutive_errors_ = 0;
    transition_to(RsmState::Receiving);

    event_bus::EventBus::instance().publish(
        event_bus::EventType::RadioRecovered);

    return common::Result<void>::ok();
}

void RadioStateMachine::tick() {
    if (!initialized_ || !auto_recovery_) return;

    auto& radio = radio_cc1101::RadioCc1101::instance();

    if (radio.state() == radio_cc1101::RadioState::Error) {
        if (consecutive_errors_ < kMaxConsecutiveErrors) {
            recover();
        } else {
#ifndef HOST_TEST_BUILD
            ESP_LOGE(TAG, "Max consecutive errors reached (%lu), "
                     "radio recovery disabled until manual intervention",
                     (unsigned long)kMaxConsecutiveErrors);
#endif
            transition_to(RsmState::Error);
        }
    }
}

void RadioStateMachine::transition_to(RsmState new_state) {
    if (state_ == new_state) return;

#ifndef HOST_TEST_BUILD
    static const char* state_names[] = {
        "Uninitialized", "Initializing", "Receiving", "Error", "Recovering"};
    ESP_LOGD(TAG, "State: %s -> %s",
             state_names[static_cast<int>(state_)],
             state_names[static_cast<int>(new_state)]);
#endif

    state_ = new_state;
}

} // namespace radio_state_machine
