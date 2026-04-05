#include "api_handlers/config_json_codec.hpp"

#include "auth_service/auth_service.hpp"
#include "config_store/config_store.hpp"
#include "protocol_driver/protocol_ids.hpp"

#include "cJSON.h"

#include <cctype>
#include <cstring>
#include <memory>
#include <string>

namespace api_handlers::detail {

namespace {

using JsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
using JsonStringPtr = std::unique_ptr<char, decltype(&cJSON_free)>;

bool copy_json_string(char* dest, size_t dest_sz, const cJSON* item) {
    if (!dest || dest_sz == 0 || !item || !cJSON_IsString(item) || !item->valuestring) {
        return false;
    }
    std::strncpy(dest, item->valuestring, dest_sz - 1);
    dest[dest_sz - 1] = '\0';
    return true;
}

void assign_admin_password_hash(config_store::AuthConfig& auth, const char* hash_cstr) {
    if (!hash_cstr) {
        auth.admin_password_hash[0] = '\0';
        return;
    }
    std::strncpy(auth.admin_password_hash, hash_cstr, sizeof(auth.admin_password_hash) - 1);
    auth.admin_password_hash[sizeof(auth.admin_password_hash) - 1] = '\0';
}

std::string to_unformatted_json(cJSON* root) {
    if (!root) {
        return "{}";
    }
    JsonStringPtr printed(cJSON_PrintUnformatted(root), cJSON_free);
    if (!printed) {
        return "{}";
    }
    return std::string(printed.get());
}

bool parse_bool_like(const cJSON* item, bool& out) {
    if (!item) {
        return false;
    }
    if (cJSON_IsBool(item)) {
        out = cJSON_IsTrue(item);
        return true;
    }
    if (cJSON_IsNumber(item)) {
        out = item->valuedouble != 0;
        return true;
    }
    return false;
}

std::string lower_copy(const char* value) {
    std::string out = value ? value : "";
    for (char& ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

bool parse_scheduler_mode(const cJSON* item, protocol_driver::RadioSchedulerMode& out) {
    if (!item) {
        return false;
    }
    if (cJSON_IsNumber(item)) {
        const int value = static_cast<int>(item->valuedouble);
        if (value >= 0 && value <= 255) {
            out = static_cast<protocol_driver::RadioSchedulerMode>(value);
            return true;
        }
        return false;
    }
    if (!cJSON_IsString(item) || !item->valuestring) {
        return false;
    }

    const std::string value = lower_copy(item->valuestring);
    if (value == "locked") {
        out = protocol_driver::RadioSchedulerMode::Locked;
        return true;
    }
    if (value == "priority") {
        out = protocol_driver::RadioSchedulerMode::Priority;
        return true;
    }
    if (value == "scan") {
        out = protocol_driver::RadioSchedulerMode::Scan;
        return true;
    }
    return false;
}

bool profile_name_to_mask_bit(const char* value, protocol_driver::RadioProfileMask& out_bit) {
    if (!value) {
        return false;
    }
    const std::string lower = lower_copy(value);
    if (lower == "wmbust868" || lower == "tmode" || lower == "wmbus_t868") {
        out_bit = protocol_driver::kRadioProfileMaskWMbusT868;
        return true;
    }
    if (lower == "wmbuspriosr3" || lower == "priosr3" || lower == "prios_r3") {
        out_bit = protocol_driver::kRadioProfileMaskWMbusPriosR3;
        return true;
    }
    if (lower == "wmbuspriosr4" || lower == "priosr4" || lower == "prios_r4") {
        out_bit = protocol_driver::kRadioProfileMaskWMbusPriosR4;
        return true;
    }
    return false;
}

bool parse_enabled_profiles(const cJSON* item, protocol_driver::RadioProfileMask& out) {
    if (!item) {
        return false;
    }
    if (cJSON_IsNumber(item)) {
        const int value = static_cast<int>(item->valuedouble);
        if (value >= 0 && value <= 0xFF) {
            out = static_cast<protocol_driver::RadioProfileMask>(value);
            return true;
        }
        return false;
    }
    if (!cJSON_IsArray(item)) {
        return false;
    }

    protocol_driver::RadioProfileMask mask = protocol_driver::kRadioProfileMaskNone;
    cJSON* entry = nullptr;
    cJSON_ArrayForEach(entry, item) {
        protocol_driver::RadioProfileMask bit = protocol_driver::kRadioProfileMaskNone;
        if (cJSON_IsString(entry) && entry->valuestring && profile_name_to_mask_bit(entry->valuestring, bit)) {
            mask = static_cast<protocol_driver::RadioProfileMask>(mask | bit);
        } else if (cJSON_IsNumber(entry)) {
            const int value = static_cast<int>(entry->valuedouble);
            if (value >= 0 && value <= 0xFF) {
                mask = static_cast<protocol_driver::RadioProfileMask>(mask | value);
            }
        }
    }
    out = mask;
    return true;
}

} // namespace

std::string config_to_json_redacted(const config_store::AppConfig& c) {
    JsonPtr root(cJSON_CreateObject(), cJSON_Delete);
    if (!root) {
        return "{}";
    }

    cJSON_AddNumberToObject(root.get(), "version", static_cast<double>(c.version));
    cJSON* device = cJSON_AddObjectToObject(root.get(), "device");
    cJSON_AddStringToObject(device, "name", c.device.name);
    cJSON_AddStringToObject(device, "hostname", c.device.hostname);

    cJSON* wifi = cJSON_AddObjectToObject(root.get(), "wifi");
    cJSON_AddStringToObject(wifi, "ssid", c.wifi.ssid);
    cJSON_AddStringToObject(wifi, "password", config_store::kRedactedValue);
    cJSON_AddNumberToObject(wifi, "max_retries", static_cast<double>(c.wifi.max_retries));

    cJSON* mqtt = cJSON_AddObjectToObject(root.get(), "mqtt");
    cJSON_AddBoolToObject(mqtt, "enabled", c.mqtt.enabled);
    cJSON_AddStringToObject(mqtt, "host", c.mqtt.host);
    cJSON_AddNumberToObject(mqtt, "port", static_cast<double>(c.mqtt.port));
    cJSON_AddStringToObject(mqtt, "username", config_store::kRedactedValue);
    cJSON_AddStringToObject(mqtt, "password", config_store::kRedactedValue);
    cJSON_AddStringToObject(mqtt, "prefix", c.mqtt.prefix);
    cJSON_AddStringToObject(mqtt, "client_id", c.mqtt.client_id);
    cJSON_AddNumberToObject(mqtt, "qos", static_cast<double>(c.mqtt.qos));
    cJSON_AddBoolToObject(mqtt, "use_tls", c.mqtt.use_tls);

    cJSON* radio = cJSON_AddObjectToObject(root.get(), "radio");
    cJSON_AddNumberToObject(radio, "frequency_khz", static_cast<double>(c.radio.frequency_khz));
    cJSON_AddNumberToObject(radio, "data_rate", static_cast<double>(c.radio.data_rate));
    cJSON_AddBoolToObject(radio, "auto_recovery", c.radio.auto_recovery);
    cJSON_AddNumberToObject(radio, "scheduler_mode",
                            static_cast<double>(c.radio.scheduler_mode));
    cJSON_AddNumberToObject(radio, "enabled_profiles",
                            static_cast<double>(c.radio.enabled_profiles));
    cJSON_AddBoolToObject(radio, "prios_capture_campaign", c.radio.prios_capture_campaign);
    cJSON_AddBoolToObject(radio, "prios_discovery_mode", c.radio.prios_discovery_mode);
    cJSON_AddBoolToObject(radio, "prios_manchester_enabled",
                          c.radio.prios_manchester_enabled);

    cJSON* logging = cJSON_AddObjectToObject(root.get(), "logging");
    cJSON_AddNumberToObject(logging, "level", static_cast<double>(c.logging.level));

    cJSON* auth = cJSON_AddObjectToObject(root.get(), "auth");
    cJSON_AddStringToObject(auth, "admin_password", config_store::kRedactedValue);
    cJSON_AddBoolToObject(auth, "password_set", c.auth.has_password());
    cJSON_AddNumberToObject(auth, "session_timeout_s",
                            static_cast<double>(c.auth.session_timeout_s));
    return to_unformatted_json(root.get());
}

void apply_config_json(const cJSON* root, config_store::AppConfig& cfg) {
    const cJSON* device = cJSON_GetObjectItemCaseSensitive(root, "device");
    if (device && cJSON_IsObject(device)) {
        copy_json_string(cfg.device.name, sizeof(cfg.device.name),
                         cJSON_GetObjectItemCaseSensitive(device, "name"));
        copy_json_string(cfg.device.hostname, sizeof(cfg.device.hostname),
                         cJSON_GetObjectItemCaseSensitive(device, "hostname"));
    }

    const cJSON* wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    if (wifi && cJSON_IsObject(wifi)) {
        copy_json_string(cfg.wifi.ssid, sizeof(cfg.wifi.ssid),
                         cJSON_GetObjectItemCaseSensitive(wifi, "ssid"));
        const cJSON* pw = cJSON_GetObjectItemCaseSensitive(wifi, "password");
        if (pw && cJSON_IsString(pw) && pw->valuestring &&
            std::strcmp(pw->valuestring, config_store::kRedactedValue) != 0) {
            copy_json_string(cfg.wifi.password, sizeof(cfg.wifi.password), pw);
        }
        const cJSON* mr = cJSON_GetObjectItemCaseSensitive(wifi, "max_retries");
        if (mr && cJSON_IsNumber(mr)) {
            const int value = static_cast<int>(mr->valuedouble);
            if (value >= 0 && value <= 255) {
                cfg.wifi.max_retries = static_cast<uint8_t>(value);
            }
        }
    }

    const cJSON* mqtt = cJSON_GetObjectItemCaseSensitive(root, "mqtt");
    if (mqtt && cJSON_IsObject(mqtt)) {
        bool bool_value = false;
        if (parse_bool_like(cJSON_GetObjectItemCaseSensitive(mqtt, "enabled"), bool_value)) {
            cfg.mqtt.enabled = bool_value;
        }
        copy_json_string(cfg.mqtt.host, sizeof(cfg.mqtt.host),
                         cJSON_GetObjectItemCaseSensitive(mqtt, "host"));
        const cJSON* port = cJSON_GetObjectItemCaseSensitive(mqtt, "port");
        if (port && cJSON_IsNumber(port)) {
            const int value = static_cast<int>(port->valuedouble);
            if (value > 0 && value <= 65535) {
                cfg.mqtt.port = static_cast<uint16_t>(value);
            }
        }
        const cJSON* user = cJSON_GetObjectItemCaseSensitive(mqtt, "username");
        if (user && cJSON_IsString(user) && user->valuestring &&
            std::strcmp(user->valuestring, config_store::kRedactedValue) != 0) {
            copy_json_string(cfg.mqtt.username, sizeof(cfg.mqtt.username), user);
        }
        const cJSON* mpw = cJSON_GetObjectItemCaseSensitive(mqtt, "password");
        if (mpw && cJSON_IsString(mpw) && mpw->valuestring &&
            std::strcmp(mpw->valuestring, config_store::kRedactedValue) != 0) {
            copy_json_string(cfg.mqtt.password, sizeof(cfg.mqtt.password), mpw);
        }
        copy_json_string(cfg.mqtt.prefix, sizeof(cfg.mqtt.prefix),
                         cJSON_GetObjectItemCaseSensitive(mqtt, "prefix"));
        copy_json_string(cfg.mqtt.client_id, sizeof(cfg.mqtt.client_id),
                         cJSON_GetObjectItemCaseSensitive(mqtt, "client_id"));
        const cJSON* qos = cJSON_GetObjectItemCaseSensitive(mqtt, "qos");
        if (qos && cJSON_IsNumber(qos)) {
            const int value = static_cast<int>(qos->valuedouble);
            if (value >= 0 && value <= 2) {
                cfg.mqtt.qos = static_cast<uint8_t>(value);
            }
        }
        if (parse_bool_like(cJSON_GetObjectItemCaseSensitive(mqtt, "use_tls"), bool_value)) {
            cfg.mqtt.use_tls = bool_value;
        }
    }

    const cJSON* radio = cJSON_GetObjectItemCaseSensitive(root, "radio");
    if (radio && cJSON_IsObject(radio)) {
        const cJSON* fk = cJSON_GetObjectItemCaseSensitive(radio, "frequency_khz");
        if (fk && cJSON_IsNumber(fk)) {
            cfg.radio.frequency_khz = static_cast<uint32_t>(fk->valuedouble);
        }
        const cJSON* dr = cJSON_GetObjectItemCaseSensitive(radio, "data_rate");
        if (dr && cJSON_IsNumber(dr)) {
            const int value = static_cast<int>(dr->valuedouble);
            if (value >= 0 && value <= 255) {
                cfg.radio.data_rate = static_cast<uint8_t>(value);
            }
        }
        bool bool_value = false;
        if (parse_bool_like(cJSON_GetObjectItemCaseSensitive(radio, "auto_recovery"), bool_value)) {
            cfg.radio.auto_recovery = bool_value;
        }

        protocol_driver::RadioSchedulerMode scheduler_mode = cfg.radio.scheduler_mode;
        if (parse_scheduler_mode(cJSON_GetObjectItemCaseSensitive(radio, "scheduler_mode"),
                                 scheduler_mode)) {
            cfg.radio.scheduler_mode = scheduler_mode;
        }

        protocol_driver::RadioProfileMask enabled_profiles = cfg.radio.enabled_profiles;
        if (parse_enabled_profiles(cJSON_GetObjectItemCaseSensitive(radio, "enabled_profiles"),
                                   enabled_profiles)) {
            cfg.radio.enabled_profiles = enabled_profiles;
        }

        if (parse_bool_like(cJSON_GetObjectItemCaseSensitive(radio, "prios_capture_campaign"),
                            bool_value)) {
            cfg.radio.prios_capture_campaign = bool_value;
        }
        if (parse_bool_like(cJSON_GetObjectItemCaseSensitive(radio, "prios_discovery_mode"),
                            bool_value)) {
            cfg.radio.prios_discovery_mode = bool_value;
        }
        if (parse_bool_like(cJSON_GetObjectItemCaseSensitive(radio, "prios_manchester_enabled"),
                            bool_value)) {
            cfg.radio.prios_manchester_enabled = bool_value;
        }
    }

    const cJSON* logging = cJSON_GetObjectItemCaseSensitive(root, "logging");
    if (logging && cJSON_IsObject(logging)) {
        const cJSON* lev = cJSON_GetObjectItemCaseSensitive(logging, "level");
        if (lev && cJSON_IsNumber(lev)) {
            const int value = static_cast<int>(lev->valuedouble);
            if (value >= 0 && value <= 255) {
                cfg.logging.level = static_cast<uint8_t>(value);
            }
        }
    }

    const cJSON* auth = cJSON_GetObjectItemCaseSensitive(root, "auth");
    if (auth && cJSON_IsObject(auth)) {
        const cJSON* admin_password = cJSON_GetObjectItemCaseSensitive(auth, "admin_password");
        if (admin_password && cJSON_IsString(admin_password) && admin_password->valuestring &&
            std::strcmp(admin_password->valuestring, config_store::kRedactedValue) != 0 &&
            admin_password->valuestring[0] != '\0') {
            auto hash_result =
                auth_service::AuthService::hash_password(admin_password->valuestring);
            if (hash_result.is_ok()) {
                assign_admin_password_hash(cfg.auth, hash_result.value().c_str());
            }
        }
        const cJSON* st = cJSON_GetObjectItemCaseSensitive(auth, "session_timeout_s");
        if (st && cJSON_IsNumber(st)) {
            cfg.auth.session_timeout_s = static_cast<uint32_t>(st->valuedouble);
        }
    }
}

} // namespace api_handlers::detail
