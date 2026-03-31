#include "event_bus/event_bus.hpp"

#include <cassert>

int main() {
    auto init = event_bus::EventBus::instance().initialize();
    assert(!init.is_error());

    auto& bus = event_bus::EventBus::instance();
    int wifi_handler_a_calls = 0;
    int wifi_handler_b_calls = 0;
    int mqtt_handler_calls = 0;

    event_bus::SubscriptionId handler_b_id = 0;
    auto sub_a = bus.subscribe(event_bus::EventType::WifiConnected,
                               [&](const event_bus::Event&) {
                                   wifi_handler_a_calls++;
                                   auto unsub = bus.unsubscribe(handler_b_id);
                                   assert(unsub.is_ok() ||
                                          unsub.error() == common::ErrorCode::NotFound);
                                   bus.publish(event_bus::EventType::MqttConnected);
                               });
    assert(sub_a.is_ok());

    auto sub_b = bus.subscribe(event_bus::EventType::WifiConnected,
                               [&](const event_bus::Event&) { wifi_handler_b_calls++; });
    assert(sub_b.is_ok());
    handler_b_id = sub_b.value();

    auto sub_c = bus.subscribe(event_bus::EventType::MqttConnected,
                               [&](const event_bus::Event&) { mqtt_handler_calls++; });
    assert(sub_c.is_ok());

    bus.publish(event_bus::EventType::WifiConnected);
    assert(wifi_handler_a_calls == 1);
    assert(wifi_handler_b_calls == 1);
    assert(mqtt_handler_calls == 1);

    bus.publish(event_bus::EventType::WifiConnected);
    assert(wifi_handler_a_calls == 2);
    assert(wifi_handler_b_calls == 1);
    assert(mqtt_handler_calls == 2);

    return 0;
}
