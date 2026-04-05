#pragma once

#include "protocol_driver/protocol_ids.hpp"

#include <cstdint>
#include <cstring>

namespace config_store {

static constexpr uint32_t kCurrentConfigVersion = 5;
static constexpr const char* kNvsNamespace = "wg_config";
static constexpr const char* kNvsKey = "config";
static constexpr const char* kNvsBackupKey = "config_bak";

// Secret field marker used in export/API responses
static constexpr const char* kRedactedValue = "***";

struct DeviceConfig {
    char name[32];
    char hostname[32];

    static DeviceConfig make_default() {
        DeviceConfig c{};
        std::strncpy(c.name, "WMBus Gateway", sizeof(c.name) - 1);
        std::strncpy(c.hostname, "wmbus-gw", sizeof(c.hostname) - 1);
        return c;
    }
};

struct WifiConfig {
    char ssid[33];
    char password[65]; // SECRET
    uint8_t max_retries;

    static WifiConfig make_default() {
        WifiConfig c{};
        c.ssid[0] = '\0';
        c.password[0] = '\0';
        c.max_retries = 10;
        return c;
    }

    bool is_configured() const {
        return ssid[0] != '\0';
    }
};

struct MqttConfig {
    bool enabled;
    char host[128];
    uint16_t port;
    char username[64]; // SECRET
    char password[64]; // SECRET
    char prefix[64];
    char client_id[64];
    uint8_t qos;
    bool use_tls;

    static MqttConfig make_default() {
        MqttConfig c{};
        c.enabled = true;
        c.host[0] = '\0';
        c.port = 1883;
        c.username[0] = '\0';
        c.password[0] = '\0';
        std::strncpy(c.prefix, "wmbus-gw", sizeof(c.prefix) - 1);
        c.client_id[0] = '\0';
        c.qos = 0;
        c.use_tls = false;
        return c;
    }
};

struct RadioConfig {
    uint32_t frequency_khz;
    uint8_t  data_rate;      // Reserved for future mode selection
    bool     auto_recovery;

    // Scheduler mode: how the radio owner task cycles through enabled profiles.
    protocol_driver::RadioSchedulerMode scheduler_mode;

    // Bitmask of enabled RadioProfileId values (bit N = 1 << N).
    // Must not be zero; use kRadioProfileMaskWMbusT868 as the minimum.
    protocol_driver::RadioProfileMask enabled_profiles;

    // --- PRIOS experimental receive modes (v4+) ---
    //
    // When prios_capture_campaign is true:
    //   - The radio scheduler is locked to WMbusPriosR3 regardless of
    //     scheduler_mode and enabled_profiles.
    //   - T-mode reception is suspended for the duration of the campaign.
    //   - The CC1101 is reconfigured with the PRIOS R3 experimental profile.
    //   - All bounded captures are stored in PriosCaptureService.
    //
    // When prios_discovery_mode is true:
    //   - The radio scheduler is locked to WMbusPriosR3 as well.
    //   - T-mode reception is suspended for the duration of discovery.
    //   - The CC1101 uses a PRIOS sniffer/discovery profile that does not
    //     depend on the placeholder sync word.
    //   - Bounded captures are still exported through the same diagnostics
    //     and export pipeline.
    //
    // When prios_manchester_enabled is true, Variant B (Manchester ON) is
    // applied. When false, Variant A (Manchester OFF) is applied.
    // Both variants are experimental until hardware captures confirm the
    // correct modulation setting.
    bool prios_capture_campaign  = false;  // false = normal T-mode operation
    bool prios_discovery_mode    = false;  // false = use saved scheduler/profile config
    bool prios_manchester_enabled = false; // false = Variant A (Manchester off)

    static RadioConfig make_default() {
        RadioConfig c{};
        c.frequency_khz           = 868950;
        c.data_rate               = 0;
        c.auto_recovery           = true;
        c.scheduler_mode          = protocol_driver::RadioSchedulerMode::Locked;
        c.enabled_profiles        = protocol_driver::kRadioProfileMaskWMbusT868;
        c.prios_capture_campaign  = false;
        c.prios_discovery_mode    = false;
        c.prios_manchester_enabled = false;
        return c;
    }
};

struct LoggingConfig {
    uint8_t level; // Maps to esp_log_level_t (0=none, 3=info, 4=debug)

    static LoggingConfig make_default() {
        LoggingConfig c{};
        c.level = 3; // INFO
        return c;
    }
};

struct AuthConfig {
    // Longest supported format:
    // "pbkdf2$2000$" + salt_hex(64) + "$" + hash_hex(64) = 141 chars + '\0'
    char admin_password_hash[160]; // SECRET
    uint32_t session_timeout_s;

    static AuthConfig make_default() {
        AuthConfig c{};
        std::memset(c.admin_password_hash, 0, sizeof(c.admin_password_hash));
        c.session_timeout_s = 3600;
        return c;
    }

    bool has_password() const {
        return admin_password_hash[0] != '\0';
    }
};

struct AppConfig {
    uint32_t version;
    DeviceConfig device;
    WifiConfig wifi;
    MqttConfig mqtt;
    RadioConfig radio;
    LoggingConfig logging;
    AuthConfig auth;

    static AppConfig make_default() {
        AppConfig c{};
        c.version = kCurrentConfigVersion;
        c.device = DeviceConfig::make_default();
        c.wifi = WifiConfig::make_default();
        c.mqtt = MqttConfig::make_default();
        c.radio = RadioConfig::make_default();
        c.logging = LoggingConfig::make_default();
        c.auth = AuthConfig::make_default();
        return c;
    }
};

} // namespace config_store
