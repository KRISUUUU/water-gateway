#pragma once

#include "common/error.hpp"
#include "common/result.hpp"
#include <atomic>
#include <cstdint>
#include <mutex>

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
    common::Result<void> connect(const char* host, uint16_t port, const char* username,
                                 const char* password, const char* client_id, bool use_tls);

    common::Result<void> disconnect();

    // Publish a message. Returns error if not connected.
    common::Result<void> publish(const char* topic, const char* payload, int qos = 0,
                                 bool retain = false);

    MqttStatus status() const;

    MqttState state() const {
        return state_.load();
    }
    bool is_connected() const {
        return state_.load() == MqttState::Connected;
    }

    // Set the Last Will topic and payload (must be called before connect)
    void set_last_will(const char* topic, const char* payload);

  private:
    MqttService() = default;

#ifndef HOST_TEST_BUILD
    static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id,
                                   void* event_data);
    void handle_event(int32_t event_id, void* event_data);
#endif

    bool initialized_ = false;

    // Atomics for fields accessed from both the mqtt_task and the MQTT event
    // loop thread without holding a lock (hot path, low contention).
    std::atomic<MqttState> state_{MqttState::Uninitialized};
    std::atomic<uint32_t> publish_count_{0};
    std::atomic<uint32_t> publish_failures_{0};
    std::atomic<uint32_t> reconnect_count_{0};
    std::atomic<int64_t> last_publish_epoch_ms_{0};

    // Mutex protects: client_ (opaque handle) and broker_uri_ (char array).
    // Both may be read/written from multiple task contexts.
    mutable std::mutex mutex_{};

#ifndef HOST_TEST_BUILD
    void* client_ = nullptr; // esp_mqtt_client_handle_t — guarded by mutex_
#endif

    char broker_uri_[160] = {};  // guarded by mutex_
    char lwt_topic_[128] = {};   // written only before connect(), effectively immutable after
    char lwt_payload_[128] = {}; // same as lwt_topic_
};

} // namespace mqtt_service
