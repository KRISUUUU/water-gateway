#pragma once

#include <string>

#include "common/result.hpp"

namespace mqtt_service {

enum class MqttState {
    Uninitialized = 0,
    Disconnected,
    Connecting,
    Connected,
    Error
};

struct MqttStatus {
    MqttState state{MqttState::Uninitialized};
    std::string broker{};
    std::string client_id{};
    std::uint32_t reconnect_count{0};
};

class MqttService {
public:
    static MqttService& instance();

    common::Result<void> initialize();
    common::Result<void> connect();
    common::Result<void> disconnect();
    common::Result<void> publish(const std::string& topic, const std::string& payload);
    [[nodiscard]] MqttStatus status() const;

private:
    MqttService() = default;

    bool initialized_{false};
    MqttStatus status_{};
};

}  // namespace mqtt_service
