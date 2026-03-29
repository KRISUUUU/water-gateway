#include "mqtt_service/mqtt_service.hpp"
#include <cstdio>
#include <cstring>

#ifndef HOST_TEST_BUILD
#include "esp_log.h"
#include "event_bus/event_bus.hpp"
#include "mqtt_client.h"
#include <sys/time.h>

static const char* TAG = "mqtt_svc";
#endif

namespace mqtt_service {

MqttService& MqttService::instance() {
    static MqttService svc;
    return svc;
}

common::Result<void> MqttService::initialize() {
    if (initialized_) {
        return common::Result<void>::error(common::ErrorCode::AlreadyInitialized);
    }
    state_ = MqttState::Disconnected;
    publish_count_ = 0;
    publish_failures_ = 0;
    reconnect_count_ = 0;
    outbox_enqueue_failures_ = 0;
    outbox_oversize_rejections_ = 0;
    outbox_max_depth_ = 0;
    outbox_dropped_disconnected_ = 0;
    outbox_carry_pending_ = 0;
    outbox_carry_retry_attempts_ = 0;
    outbox_carry_drops_ = 0;
    last_publish_epoch_ms_ = 0;
    broker_uri_[0] = '\0';
    initialized_ = true;
    return common::Result<void>::ok();
}

void MqttService::set_last_will(const char* topic, const char* payload) {
    if (topic) {
        std::strncpy(lwt_topic_, topic, sizeof(lwt_topic_) - 1);
        lwt_topic_[sizeof(lwt_topic_) - 1] = '\0';
    }
    if (payload) {
        std::strncpy(lwt_payload_, payload, sizeof(lwt_payload_) - 1);
        lwt_payload_[sizeof(lwt_payload_) - 1] = '\0';
    }
}

common::Result<void> MqttService::connect(const char* host, uint16_t port, const char* username,
                                          const char* password, const char* client_id,
                                          bool use_tls) {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    if (!host || host[0] == '\0') {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }

    const char* scheme = use_tls ? "mqtts" : "mqtt";
    std::snprintf(broker_uri_, sizeof(broker_uri_), "%s://%s:%u", scheme, host, port);

#ifndef HOST_TEST_BUILD
    esp_mqtt_client_config_t mqtt_cfg{};
    mqtt_cfg.broker.address.uri = broker_uri_;

    if (username && username[0] != '\0') {
        mqtt_cfg.credentials.username = username;
    }
    if (password && password[0] != '\0') {
        mqtt_cfg.credentials.authentication.password = password;
    }
    if (client_id && client_id[0] != '\0') {
        mqtt_cfg.credentials.client_id = client_id;
    }

    if (lwt_topic_[0] != '\0') {
        mqtt_cfg.session.last_will.topic = lwt_topic_;
        mqtt_cfg.session.last_will.msg = lwt_payload_;
        mqtt_cfg.session.last_will.qos = 0;
        mqtt_cfg.session.last_will.retain = 1;
    }

    mqtt_cfg.network.reconnect_timeout_ms = 1000;
    mqtt_cfg.session.keepalive = 30;

    if (client_) {
        esp_mqtt_client_destroy(static_cast<esp_mqtt_client_handle_t>(client_));
        client_ = nullptr;
    }

    client_ = esp_mqtt_client_init(&mqtt_cfg);
    if (!client_) {
        ESP_LOGE(TAG, "MQTT client init failed");
        state_ = MqttState::Error;
        return common::Result<void>::error(common::ErrorCode::MqttConnectFailed);
    }

    esp_mqtt_client_register_event(static_cast<esp_mqtt_client_handle_t>(client_),
                                   static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),
                                   mqtt_event_handler, this);

    esp_err_t err = esp_mqtt_client_start(static_cast<esp_mqtt_client_handle_t>(client_));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT client start failed: %d", err);
        state_ = MqttState::Error;
        return common::Result<void>::error(common::ErrorCode::MqttConnectFailed);
    }

    ESP_LOGI(TAG, "MQTT connecting to %s", broker_uri_);
#endif

    state_ = MqttState::Connecting;
    return common::Result<void>::ok();
}

common::Result<void> MqttService::disconnect() {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }

#ifndef HOST_TEST_BUILD
    if (client_) {
        esp_mqtt_client_stop(static_cast<esp_mqtt_client_handle_t>(client_));
        esp_mqtt_client_destroy(static_cast<esp_mqtt_client_handle_t>(client_));
        client_ = nullptr;
    }
#endif

    state_ = MqttState::Disconnected;
    return common::Result<void>::ok();
}

common::Result<void> MqttService::publish(const char* topic, const char* payload, int qos,
                                          bool retain) {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    if (state_ != MqttState::Connected) {
        publish_failures_++;
        return common::Result<void>::error(common::ErrorCode::MqttNotConnected);
    }
    if (!topic || !payload) {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }

#ifndef HOST_TEST_BUILD
    int msg_id = esp_mqtt_client_publish(static_cast<esp_mqtt_client_handle_t>(client_), topic,
                                         payload, 0, qos, retain ? 1 : 0);

    if (msg_id < 0) {
        publish_failures_++;
        return common::Result<void>::error(common::ErrorCode::MqttPublishFailed);
    }

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    last_publish_epoch_ms_ =
        static_cast<int64_t>(tv.tv_sec) * 1000 + static_cast<int64_t>(tv.tv_usec) / 1000;
#endif

    publish_count_++;
    return common::Result<void>::ok();
}

MqttStatus MqttService::status() const {
    MqttStatus s{};
    s.state = state_;
    s.publish_count = publish_count_;
    s.publish_failures = publish_failures_;
    s.reconnect_count = reconnect_count_;
    s.outbox_enqueue_failures = outbox_enqueue_failures_;
    s.outbox_oversize_rejections = outbox_oversize_rejections_;
    s.outbox_max_depth = outbox_max_depth_;
    s.outbox_dropped_disconnected = outbox_dropped_disconnected_;
    s.outbox_carry_pending = outbox_carry_pending_;
    s.outbox_carry_retry_attempts = outbox_carry_retry_attempts_;
    s.outbox_carry_drops = outbox_carry_drops_;
    s.last_publish_epoch_ms = last_publish_epoch_ms_;
    std::strncpy(s.broker_uri, broker_uri_, sizeof(s.broker_uri) - 1);
    return s;
}

void MqttService::report_outbox_enqueue_failure(bool oversize) {
    ++outbox_enqueue_failures_;
    if (oversize) {
        ++outbox_oversize_rejections_;
    }
}

void MqttService::report_outbox_depth(uint32_t depth) {
    if (depth > outbox_max_depth_) {
        outbox_max_depth_ = depth;
    }
}

void MqttService::report_outbox_dropped_disconnected() {
    ++outbox_dropped_disconnected_;
}

void MqttService::report_outbox_carry_pending(bool pending) {
    outbox_carry_pending_ = pending ? 1U : 0U;
}

void MqttService::report_outbox_carry_retry_attempt() {
    ++outbox_carry_retry_attempts_;
}

void MqttService::report_outbox_carry_drop() {
    ++outbox_carry_drops_;
}

#ifndef HOST_TEST_BUILD

void MqttService::mqtt_event_handler(void* handler_args, esp_event_base_t /*base*/,
                                     int32_t event_id, void* event_data) {
    auto* self = static_cast<MqttService*>(handler_args);
    self->handle_event(event_id, event_data);
}

void MqttService::handle_event(int32_t event_id, void* /*event_data*/) {
    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected to %s", broker_uri_);
        state_ = MqttState::Connected;
        event_bus::EventBus::instance().publish(event_bus::EventType::MqttConnected);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        if (state_ == MqttState::Connected) {
            reconnect_count_++;
        }
        state_ = MqttState::Disconnected;
        event_bus::EventBus::instance().publish(event_bus::EventType::MqttDisconnected);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error event");
        state_ = MqttState::Error;
        break;

    case MQTT_EVENT_BEFORE_CONNECT:
        state_ = MqttState::Connecting;
        break;

    default:
        break;
    }
}

#endif // HOST_TEST_BUILD

} // namespace mqtt_service
