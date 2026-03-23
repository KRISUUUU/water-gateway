#include "event_bus/event_bus.hpp"

namespace event_bus {

EventBus& EventBus::instance() {
    static EventBus bus;
    return bus;
}

common::Result<void> EventBus::initialize() {
    if (initialized_) {
        return common::Result<void>(common::ErrorCode::AlreadyInitialized);
    }

    initialized_ = true;
    return common::Result<void>();
}

common::Result<std::uint32_t> EventBus::subscribe(EventType type, EventHandler handler) {
    if (!initialized_) {
        return common::Result<std::uint32_t>(common::ErrorCode::NotInitialized);
    }

    if (!handler) {
        return common::Result<std::uint32_t>(common::ErrorCode::InvalidArgument);
    }

    const std::uint32_t id = next_subscription_id_++;
    handlers_[type][id] = handler;
    return common::Result<std::uint32_t>(id);
}

common::Result<void> EventBus::unsubscribe(EventType type, std::uint32_t subscription_id) {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    auto type_it = handlers_.find(type);
    if (type_it == handlers_.end()) {
        return common::Result<void>(common::ErrorCode::NotFound);
    }

    auto& subscriptions = type_it->second;
    auto sub_it = subscriptions.find(subscription_id);
    if (sub_it == subscriptions.end()) {
        return common::Result<void>(common::ErrorCode::NotFound);
    }

    subscriptions.erase(sub_it);
    return common::Result<void>();
}

common::Result<void> EventBus::publish(const Event& event) {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    auto type_it = handlers_.find(event.type);
    if (type_it == handlers_.end()) {
        return common::Result<void>();
    }

    for (const auto& [_, handler] : type_it->second) {
        if (handler) {
            handler(event);
        }
    }

    return common::Result<void>();
}

}  // namespace event_bus
