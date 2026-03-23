#include "event_bus/event_bus.hpp"

#ifndef HOST_TEST_BUILD
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
static const char* TAG = "event_bus";
#endif

namespace event_bus {

EventBus& EventBus::instance() {
    static EventBus bus;
    return bus;
}

common::Result<void> EventBus::initialize() {
    if (initialized_) {
        return common::Result<void>::error(common::ErrorCode::AlreadyInitialized);
    }

#ifndef HOST_TEST_BUILD
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) {
        ESP_LOGE(TAG, "Failed to create event bus mutex");
        return common::Result<void>::error(common::ErrorCode::Unknown);
    }
#endif

    for (size_t i = 0; i < kMaxSubscriptions; ++i) {
        subscriptions_[i].active = false;
    }
    sub_count_ = 0;
    next_id_ = 1;
    initialized_ = true;
    return common::Result<void>::ok();
}

common::Result<SubscriptionId> EventBus::subscribe(EventType type,
                                                    EventHandler handler) {
    if (!initialized_) {
        return common::Result<SubscriptionId>::error(
            common::ErrorCode::NotInitialized);
    }
    if (!handler) {
        return common::Result<SubscriptionId>::error(
            common::ErrorCode::InvalidArgument);
    }

#ifndef HOST_TEST_BUILD
    xSemaphoreTake(static_cast<SemaphoreHandle_t>(mutex_), portMAX_DELAY);
#endif

    SubscriptionId assigned_id = 0;
    bool found_slot = false;

    for (size_t i = 0; i < kMaxSubscriptions; ++i) {
        if (!subscriptions_[i].active) {
            subscriptions_[i].id = next_id_++;
            subscriptions_[i].type = type;
            subscriptions_[i].handler = handler;
            subscriptions_[i].active = true;
            assigned_id = subscriptions_[i].id;
            sub_count_++;
            found_slot = true;
            break;
        }
    }

#ifndef HOST_TEST_BUILD
    xSemaphoreGive(static_cast<SemaphoreHandle_t>(mutex_));
#endif

    if (!found_slot) {
        return common::Result<SubscriptionId>::error(
            common::ErrorCode::BufferFull);
    }
    return common::Result<SubscriptionId>::ok(assigned_id);
}

common::Result<void> EventBus::unsubscribe(SubscriptionId id) {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }

#ifndef HOST_TEST_BUILD
    xSemaphoreTake(static_cast<SemaphoreHandle_t>(mutex_), portMAX_DELAY);
#endif

    bool found = false;
    for (size_t i = 0; i < kMaxSubscriptions; ++i) {
        if (subscriptions_[i].active && subscriptions_[i].id == id) {
            subscriptions_[i].active = false;
            subscriptions_[i].handler = nullptr;
            sub_count_--;
            found = true;
            break;
        }
    }

#ifndef HOST_TEST_BUILD
    xSemaphoreGive(static_cast<SemaphoreHandle_t>(mutex_));
#endif

    return found ? common::Result<void>::ok()
                 : common::Result<void>::error(common::ErrorCode::NotFound);
}

void EventBus::publish(const Event& event) {
    if (!initialized_) {
        return;
    }

#ifndef HOST_TEST_BUILD
    xSemaphoreTake(static_cast<SemaphoreHandle_t>(mutex_), portMAX_DELAY);
#endif

    for (size_t i = 0; i < kMaxSubscriptions; ++i) {
        if (subscriptions_[i].active &&
            subscriptions_[i].type == event.type &&
            subscriptions_[i].handler) {
            subscriptions_[i].handler(event);
        }
    }

#ifndef HOST_TEST_BUILD
    xSemaphoreGive(static_cast<SemaphoreHandle_t>(mutex_));
#endif
}

void EventBus::publish(EventType type, int32_t code) {
    Event event{};
    event.type = type;
    event.code = code;
    event.data = nullptr;
    event.data_len = 0;
    publish(event);
}

size_t EventBus::subscription_count() const {
    return sub_count_;
}

} // namespace event_bus
