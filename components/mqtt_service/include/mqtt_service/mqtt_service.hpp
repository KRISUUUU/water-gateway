#pragma once

#include "common/error.hpp"
#include "common/result.hpp"
#include <cstdint>

#ifndef HOST_TEST_BUILD
#include "esp_event_base.h"
#endif

namespace mqtt_service {

enum class MqttState : uint8_t {
    Uninitialized = 0,
    Disconnected,
    Connecting,
    Connected,
    Error,
};

struct MqttStatus {
    MqttState state;
    uint32_t publish_count;
    uint32_t publish_failures;
    uint32_t reconnect_count;
    int64_t last_publish_epoch_ms;
    char broker_uri[160]; // "mqtt://host:port" (redacted in export)
};

class MqttService {
public:
    static MqttService& instance();

    common::Result<void> initialize();

    // Connect to broker using current config.
    // Non-blocking: starts connection, actual connect happens asynchronously.
    common::Result<void> connect(const char* host, uint16_t port,
                                 const char* username, const char* password,
                                 const char* client_id, bool use_tls);

    common::Result<void> disconnect();

    // Publish a message. Returns error if not connected.
    // QoS is applied from the service-level config.
    common::Result<void> publish(const char* topic, const char* payload,
                                 int qos = 0, bool retain = false);

    MqttStatus status() const;
    MqttState state() const { return state_; }
    bool is_connected() const { return state_ == MqttState::Connected; }

    // Set the Last Will topic and payload (must be called before connect)
    void set_last_will(const char* topic, const char* payload);

private:
    MqttService() = default;

#ifndef HOST_TEST_BUILD
    static void mqtt_event_handler(void* handler_args,
                                   esp_event_base_t base,
                                   int32_t event_id,
                                   void* event_data);
    void handle_event(int32_t event_id, void* event_data);

    void* client_ = nullptr; // esp_mqtt_client_handle_t
#endif

    bool initialized_ = false;
    MqttState state_ = MqttState::Uninitialized;
    uint32_t publish_count_ = 0;
    uint32_t publish_failures_ = 0;
    uint32_t reconnect_count_ = 0;
    int64_t last_publish_epoch_ms_ = 0;
    char broker_uri_[160] = {};
    char lwt_topic_[128] = {};
    char lwt_payload_[128] = {};
};

} // namespace mqtt_service
