#pragma once

#include "common/error.hpp"
#include "common/result.hpp"
#include <cstdint>
#include <functional>

namespace event_bus {

enum class EventType : uint8_t {
    SystemBoot = 0,
    WifiConnected,
    WifiDisconnected,
    MqttConnected,
    MqttDisconnected,
    RadioError,
    RadioRecovered,
    ConfigChanged,
    OtaStarted,
    OtaCompleted,
    HealthStateChanged,
    ProvisioningStarted,
    ProvisioningCompleted,
};

struct Event {
    EventType type;
    int32_t code;     // Optional numeric payload (e.g., error code, reason)
    const void* data; // Optional pointer payload (lifetime: only valid during handler call)
    uint16_t data_len;
};

using SubscriptionId = uint16_t;
using EventHandler = std::function<void(const Event&)>;

static constexpr size_t kMaxSubscriptions = 32;

// In-process event bus for loosely-coupled module communication.
//
// Thread safety: publish() and subscribe() acquire a mutex internally.
// publish() snapshots matching handlers under the mutex, then releases it
// before invoking handlers synchronously, so handlers MUST still be fast
// (no blocking, no heavy computation).
// If a handler needs to do significant work, it should post to a
// FreeRTOS queue and return immediately.
class EventBus {
  public:
    static EventBus& instance();

    common::Result<void> initialize();

    common::Result<SubscriptionId> subscribe(EventType type, EventHandler handler);
    common::Result<void> unsubscribe(SubscriptionId id);

    void publish(const Event& event);

    // Convenience: publish with just type and optional code
    void publish(EventType type, int32_t code = 0);

    size_t subscription_count() const;
    bool is_initialized() const {
        return initialized_;
    }

  private:
    EventBus() = default;

    struct Subscription {
        SubscriptionId id;
        EventType type;
        EventHandler handler;
        bool active;
    };

    bool initialized_ = false;
    Subscription subscriptions_[kMaxSubscriptions]{};
    size_t sub_count_ = 0;
    SubscriptionId next_id_ = 1;

#ifndef HOST_TEST_BUILD
    void* mutex_ = nullptr; // SemaphoreHandle_t, opaque to avoid header dep
#endif
};

} // namespace event_bus
