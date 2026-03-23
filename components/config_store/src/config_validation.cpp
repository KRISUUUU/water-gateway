#include "config_store/config_validation.hpp"

namespace config_store {

ValidationResult validate_config(const AppConfig& config) {
    ValidationResult result{};

    if (config.device.device_name.empty()) {
        result.valid = false;
        result.issues.push_back({"device.device_name", "Device name must not be empty"});
    }

    if (config.device.hostname.empty()) {
        result.valid = false;
        result.issues.push_back({"device.hostname", "Hostname must not be empty"});
    }

    if (config.mqtt.enabled) {
        if (config.mqtt.broker_host.empty()) {
            result.valid = false;
            result.issues.push_back({"mqtt.broker_host", "MQTT broker host must not be empty"});
        }

        if (config.mqtt.broker_port == 0) {
            result.valid = false;
            result.issues.push_back({"mqtt.broker_port", "MQTT broker port must be non-zero"});
        }

        if (config.mqtt.topic_prefix.empty()) {
            result.valid = false;
            result.issues.push_back({"mqtt.topic_prefix", "MQTT topic prefix must not be empty"});
        }
    }

    if (config.radio.frequency_mhz < 300.0 || config.radio.frequency_mhz > 1000.0) {
        result.valid = false;
        result.issues.push_back({"radio.frequency_mhz", "Radio frequency is outside supported range"});
    }

    return result;
}

}  // namespace config_store
