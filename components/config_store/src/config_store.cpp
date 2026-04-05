#include "config_store/config_store.hpp"
#include "config_store/config_migration.hpp"
#include "config_store/config_validation.hpp"
#include "event_bus/event_bus.hpp"

#ifndef HOST_TEST_BUILD
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
static const char* TAG = "config_store";
#endif

namespace config_store {

namespace {
#ifndef HOST_TEST_BUILD
struct AuthConfigV1 {
    char admin_password_hash[98];
    uint32_t session_timeout_s;
};

struct AppConfigV1 {
    uint32_t version;
    DeviceConfig device;
    WifiConfig wifi;
    MqttConfig mqtt;
    RadioConfig radio;
    LoggingConfig logging;
    AuthConfigV1 auth;
};

AppConfig convert_v1_blob(const AppConfigV1& old_cfg) {
    AppConfig converted = AppConfig::make_default();
    converted.version = old_cfg.version;
    converted.device = old_cfg.device;
    converted.wifi = old_cfg.wifi;
    converted.mqtt = old_cfg.mqtt;
    converted.radio = old_cfg.radio;
    converted.logging = old_cfg.logging;
    std::memcpy(converted.auth.admin_password_hash, old_cfg.auth.admin_password_hash,
                sizeof(old_cfg.auth.admin_password_hash));
    converted.auth.admin_password_hash[sizeof(converted.auth.admin_password_hash) - 1] = '\0';
    converted.auth.session_timeout_s = old_cfg.auth.session_timeout_s;
    return converted;
}
#endif

void normalize_config_strings(AppConfig& cfg) {
    cfg.device.name[sizeof(cfg.device.name) - 1] = '\0';
    cfg.device.hostname[sizeof(cfg.device.hostname) - 1] = '\0';

    cfg.wifi.ssid[sizeof(cfg.wifi.ssid) - 1] = '\0';
    cfg.wifi.password[sizeof(cfg.wifi.password) - 1] = '\0';

    cfg.mqtt.host[sizeof(cfg.mqtt.host) - 1] = '\0';
    cfg.mqtt.username[sizeof(cfg.mqtt.username) - 1] = '\0';
    cfg.mqtt.password[sizeof(cfg.mqtt.password) - 1] = '\0';
    cfg.mqtt.prefix[sizeof(cfg.mqtt.prefix) - 1] = '\0';
    cfg.mqtt.client_id[sizeof(cfg.mqtt.client_id) - 1] = '\0';

    cfg.auth.admin_password_hash[sizeof(cfg.auth.admin_password_hash) - 1] = '\0';
}

#ifndef HOST_TEST_BUILD
common::ErrorCode read_blob_from_nvs(nvs_handle_t handle, const char* key, AppConfig& out_cfg) {
    size_t required_size = 0;
    esp_err_t err = nvs_get_blob(handle, key, nullptr, &required_size);
    if (err != ESP_OK) {
        return common::ErrorCode::NvsReadFailed;
    }
    if (required_size == sizeof(AppConfig)) {
        err = nvs_get_blob(handle, key, &out_cfg, &required_size);
        return err == ESP_OK ? common::ErrorCode::OK : common::ErrorCode::NvsReadFailed;
    }
    if (required_size == sizeof(AppConfigV1)) {
        AppConfigV1 legacy{};
        err = nvs_get_blob(handle, key, &legacy, &required_size);
        if (err != ESP_OK) {
            return common::ErrorCode::NvsReadFailed;
        }
        out_cfg = convert_v1_blob(legacy);
        return common::ErrorCode::OK;
    }
    return common::ErrorCode::StorageCorrupted;
}
#endif

#ifndef HOST_TEST_BUILD
struct ConfigStoreMutexGuard {
    explicit ConfigStoreMutexGuard(void* mutex)
        : handle_(static_cast<SemaphoreHandle_t>(mutex)) {
        if (handle_) {
            xSemaphoreTake(handle_, portMAX_DELAY);
        }
    }

    ~ConfigStoreMutexGuard() {
        if (handle_) {
            xSemaphoreGive(handle_);
        }
    }

    SemaphoreHandle_t handle_;
};
#endif
} // namespace

ConfigStore& ConfigStore::instance() {
    static ConfigStore store;
    return store;
}

common::Result<void> ConfigStore::initialize() {
    if (initialized_) {
        return common::Result<void>::error(common::ErrorCode::AlreadyInitialized);
    }

#ifndef HOST_TEST_BUILD
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) {
        ESP_LOGE(TAG, "Failed to create config store mutex");
        return common::Result<void>::error(common::ErrorCode::Unknown);
    }

    // Initialize NVS flash if not already done
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition corrupted or version mismatch, erasing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS flash init failed: %d", err);
        return common::Result<void>::error(common::ErrorCode::NvsOpenFailed);
    }
#endif
    {
#ifndef HOST_TEST_BUILD
        ConfigStoreMutexGuard guard(mutex_);
#endif
        runtime_status_.initialize_count++;
        initialized_ = true;
        runtime_status_.load_attempts++;
        runtime_status_.used_defaults = false;
        runtime_status_.defaults_persisted = false;
        runtime_status_.defaults_persist_deferred = false;
        runtime_status_.loaded_from_backup = false;
        runtime_status_.load_source = ConfigLoadSource::None;
        runtime_status_.last_load_error = common::ErrorCode::OK;
    }

    auto load_result = load_from_nvs();
    if (load_result.is_error()) {
        {
#ifndef HOST_TEST_BUILD
            ConfigStoreMutexGuard guard(mutex_);
#endif
            runtime_status_.load_failures++;
            runtime_status_.last_load_error = load_result.error();
        }
#ifndef HOST_TEST_BUILD
        ESP_LOGW(TAG, "Config load failed (%s/%d), applying defaults in RAM",
                 common::error_code_to_string(load_result.error()),
                 static_cast<int>(load_result.error()));
#endif
        AppConfig defaults = AppConfig::make_default();
        normalize_config_strings(defaults);
        {
#ifndef HOST_TEST_BUILD
            ConfigStoreMutexGuard guard(mutex_);
#endif
            config_ = defaults;
            runtime_status_.used_defaults = true;
            runtime_status_.load_source = ConfigLoadSource::Defaults;
        }

        if (load_result.error() == common::ErrorCode::ConfigVersionMismatch) {
            {
#ifndef HOST_TEST_BUILD
                ConfigStoreMutexGuard guard(mutex_);
#endif
                runtime_status_.defaults_persist_deferred = true;
            }
#ifndef HOST_TEST_BUILD
            ESP_LOGW(TAG,
                     "Defaults persistence deferred because stored config is newer than this firmware");
#endif
        } else {
            auto persist_result = persist_to_nvs(defaults);
            if (persist_result.is_error()) {
#ifndef HOST_TEST_BUILD
                ESP_LOGE(TAG, "Failed to persist default config (%s/%d)",
                         common::error_code_to_string(persist_result.error()),
                         static_cast<int>(persist_result.error()));
#endif
                return persist_result;
            }
            {
#ifndef HOST_TEST_BUILD
                ConfigStoreMutexGuard guard(mutex_);
#endif
                runtime_status_.defaults_persisted = true;
            }
        }
    }

    loaded_ = true;
    return common::Result<void>::ok();
}

AppConfig ConfigStore::config() const {
#ifndef HOST_TEST_BUILD
    ConfigStoreMutexGuard guard(const_cast<void*>(mutex_));
#endif

    AppConfig copy = config_;
    return copy;
}

bool ConfigStore::wifi_is_configured() const {
#ifndef HOST_TEST_BUILD
    ConfigStoreMutexGuard guard(const_cast<void*>(mutex_));
#endif

    const bool configured = config_.wifi.is_configured();
    return configured;
}

ConfigRuntimeStatus ConfigStore::runtime_status() const {
#ifndef HOST_TEST_BUILD
    ConfigStoreMutexGuard guard(const_cast<void*>(mutex_));
#endif

    ConfigRuntimeStatus copy = runtime_status_;
    return copy;
}

common::Result<ValidationResult> ConfigStore::save(const AppConfig& new_config) {
    if (!initialized_) {
        return common::Result<ValidationResult>::error(common::ErrorCode::NotInitialized);
    }
    {
#ifndef HOST_TEST_BUILD
        ConfigStoreMutexGuard guard(mutex_);
#endif
        runtime_status_.save_attempts++;
    }

    AppConfig candidate = new_config;
    normalize_config_strings(candidate);

    auto validation = validate_config(candidate);
    if (!validation.valid) {
        {
#ifndef HOST_TEST_BUILD
            ConfigStoreMutexGuard guard(mutex_);
#endif
            runtime_status_.save_validation_rejects++;
        }
        return common::Result<ValidationResult>::ok(validation);
    }

    {
#ifndef HOST_TEST_BUILD
        ConfigStoreMutexGuard guard(mutex_);
#endif
        runtime_status_.migration_attempts++;
    }
    auto migrated = migrate_to_current_in_place(candidate);
    if (migrated.is_error()) {
        {
#ifndef HOST_TEST_BUILD
            ConfigStoreMutexGuard guard(mutex_);
#endif
            runtime_status_.migration_failures++;
            runtime_status_.last_migration_error = migrated.error();
            runtime_status_.save_failures++;
        }
        return common::Result<ValidationResult>::error(migrated.error());
    }
    {
#ifndef HOST_TEST_BUILD
        ConfigStoreMutexGuard guard(mutex_);
#endif
        runtime_status_.last_migration_error = common::ErrorCode::OK;
    }

    normalize_config_strings(candidate);

    // Defensive: ensure migration output is still valid before persistence.
    auto migrated_validation = validate_config(candidate);
    if (!migrated_validation.valid) {
        {
#ifndef HOST_TEST_BUILD
            ConfigStoreMutexGuard guard(mutex_);
#endif
            runtime_status_.save_validation_rejects++;
        }
        return common::Result<ValidationResult>::ok(migrated_validation);
    }

    auto persist_result = persist_to_nvs(candidate);
    if (persist_result.is_error()) {
        {
#ifndef HOST_TEST_BUILD
            ConfigStoreMutexGuard guard(mutex_);
#endif
            runtime_status_.save_failures++;
        }
        return common::Result<ValidationResult>::error(persist_result.error());
    }

    {
#ifndef HOST_TEST_BUILD
        ConfigStoreMutexGuard guard(mutex_);
#endif
        config_ = candidate;
        runtime_status_.save_successes++;
    }

    loaded_ = true;
    event_bus::EventBus::instance().publish(event_bus::EventType::ConfigChanged);
    return common::Result<ValidationResult>::ok(migrated_validation);
}

common::Result<void> ConfigStore::reset_to_defaults() {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }

    AppConfig defaults = AppConfig::make_default();
    normalize_config_strings(defaults);
    auto result = persist_to_nvs(defaults);
    if (result.is_error()) {
        return result;
    }

    {
#ifndef HOST_TEST_BUILD
        ConfigStoreMutexGuard guard(mutex_);
#endif
        config_ = defaults;
        runtime_status_.used_defaults = true;
        runtime_status_.defaults_persisted = true;
        runtime_status_.defaults_persist_deferred = false;
        runtime_status_.load_source = ConfigLoadSource::Defaults;
    }
    loaded_ = true;
    event_bus::EventBus::instance().publish(event_bus::EventType::ConfigChanged);
    return common::Result<void>::ok();
}

common::Result<void> ConfigStore::load_from_nvs() {
#ifdef HOST_TEST_BUILD
    // In host test builds, there is no NVS. Return error to trigger defaults.
    return common::Result<void>::error(common::ErrorCode::NvsReadFailed);
#else
    auto apply_loaded_config =
        [this](const AppConfig& raw_cfg, ConfigLoadSource source) -> common::Result<void> {
        AppConfig current = raw_cfg;
        normalize_config_strings(current);

        if (current.version != kCurrentConfigVersion) {
            {
#ifndef HOST_TEST_BUILD
                ConfigStoreMutexGuard guard(mutex_);
#endif
                runtime_status_.migration_attempts++;
            }
            ESP_LOGI(TAG, "Config version %lu, current %lu - migrating",
                     static_cast<unsigned long>(current.version),
                     static_cast<unsigned long>(kCurrentConfigVersion));
            auto migrated = migrate_to_current_in_place(current);
            if (migrated.is_error()) {
                {
#ifndef HOST_TEST_BUILD
                ConfigStoreMutexGuard guard(mutex_);
#endif
                    runtime_status_.migration_failures++;
                    runtime_status_.last_migration_error = migrated.error();
                }
                ESP_LOGE(TAG, "Config migration failed (%s/%d)",
                         common::error_code_to_string(migrated.error()),
                         static_cast<int>(migrated.error()));
                return common::Result<void>::error(migrated.error());
            }
            {
#ifndef HOST_TEST_BUILD
                ConfigStoreMutexGuard guard(mutex_);
#endif
                runtime_status_.last_migration_error = common::ErrorCode::OK;
            }
            normalize_config_strings(current);

            auto persist_result = persist_to_nvs(current);
            if (persist_result.is_error()) {
                ESP_LOGW(TAG, "Failed to persist migrated config (%s/%d)",
                         common::error_code_to_string(persist_result.error()),
                         static_cast<int>(persist_result.error()));
            }
        }

        auto validation = validate_config(current);
        if (!validation.valid) {
            {
#ifndef HOST_TEST_BUILD
                ConfigStoreMutexGuard guard(mutex_);
#endif
                runtime_status_.validation_failures++;
            }
            ESP_LOGE(TAG, "Stored config is invalid (%zu issues)", validation.issues.size());
            return common::Result<void>::error(common::ErrorCode::ConfigInvalid);
        }

        {
#ifndef HOST_TEST_BUILD
            ConfigStoreMutexGuard guard(mutex_);
#endif
            config_ = current;
            runtime_status_.loaded_from_backup = (source == ConfigLoadSource::BackupNvs);
            runtime_status_.load_source = source;
            runtime_status_.last_load_error = common::ErrorCode::OK;
        }
        return common::Result<void>::ok();
    };

    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "NVS namespace not found (first boot?)");
        return common::Result<void>::error(common::ErrorCode::NvsOpenFailed);
    }

    AppConfig loaded_cfg{};
    const common::ErrorCode primary_read = read_blob_from_nvs(handle, kNvsKey, loaded_cfg);
    const bool primary_ok = (primary_read == common::ErrorCode::OK);
    if (!primary_ok) {
        {
#ifndef HOST_TEST_BUILD
            ConfigStoreMutexGuard guard(mutex_);
#endif
            runtime_status_.primary_read_failures++;
        }
        ESP_LOGW(TAG, "Primary config read failed (%s/%d)",
                 common::error_code_to_string(primary_read), static_cast<int>(primary_read));
    }

    if (primary_ok) {
        auto r = apply_loaded_config(loaded_cfg, ConfigLoadSource::PrimaryNvs);
        if (r.is_ok()) {
            nvs_close(handle);
            return r;
        }
        {
#ifndef HOST_TEST_BUILD
            ConfigStoreMutexGuard guard(mutex_);
#endif
            runtime_status_.last_load_error = r.error();
        }
        ESP_LOGW(TAG, "Primary config rejected (%s/%d), trying backup",
                 common::error_code_to_string(r.error()), static_cast<int>(r.error()));
    }

    loaded_cfg = AppConfig{};
    const common::ErrorCode backup_read = read_blob_from_nvs(handle, kNvsBackupKey, loaded_cfg);
    const bool backup_ok = (backup_read == common::ErrorCode::OK);
    if (!backup_ok) {
        {
#ifndef HOST_TEST_BUILD
            ConfigStoreMutexGuard guard(mutex_);
#endif
            runtime_status_.backup_read_failures++;
        }
        ESP_LOGW(TAG, "Backup config read failed (%s/%d)",
                 common::error_code_to_string(backup_read), static_cast<int>(backup_read));
    }

    nvs_close(handle);

    if (backup_ok) {
        auto r = apply_loaded_config(loaded_cfg, ConfigLoadSource::BackupNvs);
        if (r.is_ok()) {
            ESP_LOGW(TAG, "Loaded config from backup key");
            return r;
        }
        {
#ifndef HOST_TEST_BUILD
            ConfigStoreMutexGuard guard(mutex_);
#endif
            runtime_status_.last_load_error = r.error();
        }
        return r;
    }

    return common::Result<void>::error(common::ErrorCode::NvsReadFailed);
#endif
}

common::Result<void> ConfigStore::persist_to_nvs(const AppConfig& cfg) {
#ifdef HOST_TEST_BUILD
    (void)cfg;
    runtime_status_.last_persist_error = common::ErrorCode::OK;
    return common::Result<void>::ok();
#else
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open for write failed: %d", err);
        {
            ConfigStoreMutexGuard guard(mutex_);
            runtime_status_.last_persist_error = common::ErrorCode::NvsOpenFailed;
        }
        return common::Result<void>::error(common::ErrorCode::NvsOpenFailed);
    }

    err = nvs_set_blob(handle, kNvsKey, &cfg, sizeof(AppConfig));
    if (err != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "NVS blob write failed: %d", err);
        {
            ConfigStoreMutexGuard guard(mutex_);
            runtime_status_.last_persist_error = common::ErrorCode::NvsWriteFailed;
        }
        return common::Result<void>::error(common::ErrorCode::NvsWriteFailed);
    }

    err = nvs_set_blob(handle, kNvsBackupKey, &cfg, sizeof(AppConfig));
    if (err != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "NVS backup blob write failed: %d", err);
        {
            ConfigStoreMutexGuard guard(mutex_);
            runtime_status_.last_persist_error = common::ErrorCode::NvsWriteFailed;
        }
        return common::Result<void>::error(common::ErrorCode::NvsWriteFailed);
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit failed: %d", err);
        {
            ConfigStoreMutexGuard guard(mutex_);
            runtime_status_.last_persist_error = common::ErrorCode::NvsWriteFailed;
        }
        return common::Result<void>::error(common::ErrorCode::NvsWriteFailed);
    }

    {
        ConfigStoreMutexGuard guard(mutex_);
        runtime_status_.last_persist_error = common::ErrorCode::OK;
    }
    ESP_LOGI(TAG, "Config persisted to NVS + backup (version %lu)",
             static_cast<unsigned long>(cfg.version));
    return common::Result<void>::ok();
#endif
}

} // namespace config_store
