#include "config_store/config_validation.hpp"
#include <cctype>
#include <cstring>

namespace config_store {

bool is_valid_hostname(const char* hostname) {
    if (!hostname || hostname[0] == '\0') {
        return false;
    }

    size_t len = std::strlen(hostname);
    if (len > 63) {
        return false;
    }

    if (hostname[0] == '-' || hostname[len - 1] == '-') {
        return false;
    }

    for (size_t i = 0; i < len; ++i) {
        char c = hostname[i];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') {
            return false;
        }
    }
    return true;
}

ValidationResult validate_config(const AppConfig& config) {
    ValidationResult result = ValidationResult::success();

    // --- Device section ---
    if (config.device.name[0] == '\0') {
        result.add_error("device.name", "Device name must not be empty");
    }

    if (config.device.hostname[0] == '\0') {
        result.add_error("device.hostname", "Hostname must not be empty");
    } else if (!is_valid_hostname(config.device.hostname)) {
        result.add_error("device.hostname", "Hostname must contain only lowercase letters, digits, "
                                            "and hyphens, and must not start or end with a hyphen");
    }

    // --- WiFi section ---
    // WiFi SSID can be empty (provisioning mode), but if non-empty must be reasonable
    if (config.wifi.ssid[0] != '\0') {
        size_t ssid_len = std::strlen(config.wifi.ssid);
        if (ssid_len > 32) {
            result.add_error("wifi.ssid", "SSID must not exceed 32 characters");
        }
    }

    if (config.wifi.max_retries == 0) {
        result.add_warning("wifi.max_retries",
                           "Zero retries means WiFi will not reconnect on failure");
    }

    // --- MQTT section ---
    if (config.mqtt.enabled) {
        if (config.mqtt.host[0] == '\0') {
            result.add_error("mqtt.host", "MQTT host must not be empty when MQTT is enabled");
        }

        if (config.mqtt.port == 0) {
            result.add_error("mqtt.port", "MQTT port must be between 1 and 65535");
        }

        if (config.mqtt.prefix[0] == '\0') {
            result.add_error("mqtt.prefix", "MQTT prefix must not be empty when MQTT is enabled");
        }

        if (config.mqtt.qos > 2) {
            result.add_error("mqtt.qos", "MQTT QoS must be 0, 1, or 2");
        }
    }

    // --- Radio section ---
    if (config.radio.frequency_khz < 868000 || config.radio.frequency_khz > 870000) {
        result.add_error("radio.frequency_khz",
                         "Radio frequency must be between 868000 and 870000 kHz");
    }

    // --- Auth section ---
    if (config.auth.session_timeout_s < 60) {
        result.add_error("auth.session_timeout_s", "Session timeout must be at least 60 seconds");
    } else if (config.auth.session_timeout_s > 86400) {
        result.add_error("auth.session_timeout_s",
                         "Session timeout must not exceed 86400 seconds (24 hours)");
    }

    // --- Logging section ---
    if (config.logging.level > 5) {
        result.add_error("logging.level", "Log level must be between 0 and 5");
    }

    return result;
}

} // namespace config_store
