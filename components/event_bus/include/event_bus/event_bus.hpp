#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include "common/error.hpp"
#include "common/result.hpp"

namespace event_bus {

enum class EventType : std::uint16_t {
    None = 0,
    SystemStartup,
    ConfigLoaded,
    ConfigInvalid,
    ProvisioningRequested,
    WifiStateChanged,
    MqttStateChanged,
    RadioStateChanged,
    TelegramReceived,
    OtaStateChanged,
    WarningRaised,
    ErrorRaised
};

struct Event {
    EventType type{EventType::None};
    std::string topic;
    std::string payload;
};

using EventHandler = std::function<void(const Event&)>;

class EventBus {
public:
    static EventBus& instance();

    common::Result<void> initialize();
    common::Result<std::uint32_t> subscribe(EventType type, EventHandler handler);
    common::Result<void> unsubscribe(EventType type, std::uint32_t subscription_id);
    common::Result<void> publish(const Event& event);

private:
    EventBus() = default;

    bool initialized_{false};
    std::uint32_t next_subscription_id_{1};
    std::map<EventType, std::map<std::uint32_t, EventHandler>> handlers_;
};

}  // namespace event_bus
