#pragma once

#include <cstdint>
#include <string>

namespace config_store {

inline constexpr std::uint32_t kCurrentConfigVersion = 1;

struct DeviceConfig {
    std::string device_name{"water-gateway"};
    std::string hostname{"water-gateway"};
    std::string admin_username{"admin"};
    std::string admin_password_hash{};
};

struct WifiConfig {
    bool enabled{true};
    std::string ssid{};
    std::string password{};
};

struct MqttConfig {
    bool enabled{true};
    std::string broker_host{};
    std::uint16_t broker_port{1883};
    std::string username{};
    std::string password{};
    std::string client_id{"water-gateway"};
    std::string topic_prefix{"watergw"};
};

struct RadioConfig {
    double frequency_mhz{868.95};
    std::int32_t frequency_offset_hz{0};
    bool publish_raw_frames{true};
    bool publish_diagnostics{true};
};

struct LoggingConfig {
    std::uint8_t level{1};
};

struct AppConfig {
    std::uint32_t version{kCurrentConfigVersion};
    DeviceConfig device{};
    WifiConfig wifi{};
    MqttConfig mqtt{};
    RadioConfig radio{};
    LoggingConfig logging{};
};

}  // namespace config_store
