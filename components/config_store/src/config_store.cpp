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
    size_t required_size = sizeof(AppConfig);
    const esp_err_t err = nvs_get_blob(handle, key, &out_cfg, &required_size);
    if (err != ESP_OK) {
        return common::ErrorCode::NvsReadFailed;
    }
    if (required_size != sizeof(AppConfig)) {
        return common::ErrorCode::StorageCorrupted;
    }
    return common::ErrorCode::OK;
}
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
    runtime_status_.initialize_count++;

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

    initialized_ = true;
    runtime_status_.load_attempts++;
    runtime_status_.used_defaults = false;
    runtime_status_.defaults_persisted = false;
    runtime_status_.defaults_persist_deferred = false;
    runtime_status_.loaded_from_backup = false;
    runtime_status_.load_source = ConfigLoadSource::None;
    runtime_status_.last_load_error = common::ErrorCode::OK;

    auto load_result = load_from_nvs();
    if (load_result.is_error()) {
        runtime_status_.load_failures++;
        runtime_status_.last_load_error = load_result.error();
#ifndef HOST_TEST_BUILD
        ESP_LOGW(TAG, "Config load failed (%s/%d), applying defaults in RAM",
                 common::error_code_to_string(load_result.error()),
                 static_cast<int>(load_result.error()));
#endif
        config_ = AppConfig::make_default();
        normalize_config_strings(config_);
        runtime_status_.used_defaults = true;
        runtime_status_.load_source = ConfigLoadSource::Defaults;

        if (load_result.error() == common::ErrorCode::ConfigVersionMismatch) {
            runtime_status_.defaults_persist_deferred = true;
#ifndef HOST_TEST_BUILD
            ESP_LOGW(TAG,
                     "Defaults persistence deferred because stored config is newer than this firmware");
#endif
        } else {
            auto persist_result = persist_to_nvs(config_);
            if (persist_result.is_error()) {
#ifndef HOST_TEST_BUILD
                ESP_LOGE(TAG, "Failed to persist default config (%s/%d)",
                         common::error_code_to_string(persist_result.error()),
                         static_cast<int>(persist_result.error()));
#endif
                return persist_result;
            }
            runtime_status_.defaults_persisted = true;
        }
    }

    loaded_ = true;
    return common::Result<void>::ok();
}

AppConfig ConfigStore::config() const {
#ifndef HOST_TEST_BUILD
    if (mutex_) {
        xSemaphoreTake(static_cast<SemaphoreHandle_t>(const_cast<void*>(mutex_)), portMAX_DELAY);
    }
#endif

    AppConfig copy = config_;

#ifndef HOST_TEST_BUILD
    if (mutex_) {
        xSemaphoreGive(static_cast<SemaphoreHandle_t>(const_cast<void*>(mutex_)));
    }
#endif

    return copy;
}

bool ConfigStore::wifi_is_configured() const {
#ifndef HOST_TEST_BUILD
    if (mutex_) {
        xSemaphoreTake(static_cast<SemaphoreHandle_t>(const_cast<void*>(mutex_)), portMAX_DELAY);
    }
#endif

    const bool configured = config_.wifi.is_configured();

#ifndef HOST_TEST_BUILD
    if (mutex_) {
        xSemaphoreGive(static_cast<SemaphoreHandle_t>(const_cast<void*>(mutex_)));
    }
#endif

    return configured;
}

ConfigRuntimeStatus ConfigStore::runtime_status() const {
#ifndef HOST_TEST_BUILD
    if (mutex_) {
        xSemaphoreTake(static_cast<SemaphoreHandle_t>(const_cast<void*>(mutex_)), portMAX_DELAY);
    }
#endif

    ConfigRuntimeStatus copy = runtime_status_;

#ifndef HOST_TEST_BUILD
    if (mutex_) {
        xSemaphoreGive(static_cast<SemaphoreHandle_t>(const_cast<void*>(mutex_)));
    }
#endif

    return copy;
}

common::Result<ValidationResult> ConfigStore::save(const AppConfig& new_config) {
    if (!initialized_) {
        return common::Result<ValidationResult>::error(common::ErrorCode::NotInitialized);
    }
    runtime_status_.save_attempts++;

    AppConfig candidate = new_config;
    normalize_config_strings(candidate);

    auto validation = validate_config(candidate);
    if (!validation.valid) {
        runtime_status_.save_validation_rejects++;
        return common::Result<ValidationResult>::ok(validation);
    }

    runtime_status_.migration_attempts++;
    auto migrated = migrate_to_current(candidate);
    if (migrated.is_error()) {
        runtime_status_.migration_failures++;
        runtime_status_.last_migration_error = migrated.error();
        runtime_status_.save_failures++;
        return common::Result<ValidationResult>::error(migrated.error());
    }
    runtime_status_.last_migration_error = common::ErrorCode::OK;

    AppConfig migrated_cfg = migrated.value();
    normalize_config_strings(migrated_cfg);

    // Defensive: ensure migration output is still valid before persistence.
    auto migrated_validation = validate_config(migrated_cfg);
    if (!migrated_validation.valid) {
        runtime_status_.save_validation_rejects++;
        return common::Result<ValidationResult>::ok(migrated_validation);
    }

    auto persist_result = persist_to_nvs(migrated_cfg);
    if (persist_result.is_error()) {
        runtime_status_.save_failures++;
        return common::Result<ValidationResult>::error(persist_result.error());
    }

#ifndef HOST_TEST_BUILD
    xSemaphoreTake(static_cast<SemaphoreHandle_t>(mutex_), portMAX_DELAY);
#endif
    config_ = migrated_cfg;
    runtime_status_.save_successes++;
#ifndef HOST_TEST_BUILD
    xSemaphoreGive(static_cast<SemaphoreHandle_t>(mutex_));
#endif

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

#ifndef HOST_TEST_BUILD
    xSemaphoreTake(static_cast<SemaphoreHandle_t>(mutex_), portMAX_DELAY);
#endif
    config_ = defaults;
#ifndef HOST_TEST_BUILD
    xSemaphoreGive(static_cast<SemaphoreHandle_t>(mutex_));
#endif

    loaded_ = true;
    runtime_status_.used_defaults = true;
    runtime_status_.defaults_persisted = true;
    runtime_status_.defaults_persist_deferred = false;
    runtime_status_.load_source = ConfigLoadSource::Defaults;
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
            runtime_status_.migration_attempts++;
            ESP_LOGI(TAG, "Config version %lu, current %lu - migrating",
                     static_cast<unsigned long>(current.version),
                     static_cast<unsigned long>(kCurrentConfigVersion));
            auto migrated = migrate_to_current(current);
            if (migrated.is_error()) {
                runtime_status_.migration_failures++;
                runtime_status_.last_migration_error = migrated.error();
                ESP_LOGE(TAG, "Config migration failed (%s/%d)",
                         common::error_code_to_string(migrated.error()),
                         static_cast<int>(migrated.error()));
                return common::Result<void>::error(migrated.error());
            }
            runtime_status_.last_migration_error = common::ErrorCode::OK;
            current = migrated.value();
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
            runtime_status_.validation_failures++;
            ESP_LOGE(TAG, "Stored config is invalid (%zu issues)", validation.issues.size());
            return common::Result<void>::error(common::ErrorCode::ConfigInvalid);
        }

        config_ = current;
        runtime_status_.loaded_from_backup = (source == ConfigLoadSource::BackupNvs);
        runtime_status_.load_source = source;
        runtime_status_.last_load_error = common::ErrorCode::OK;
        return common::Result<void>::ok();
    };

    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "NVS namespace not found (first boot?)");
        return common::Result<void>::error(common::ErrorCode::NvsOpenFailed);
    }

    AppConfig primary{};
    const common::ErrorCode primary_read = read_blob_from_nvs(handle, kNvsKey, primary);
    const bool primary_ok = (primary_read == common::ErrorCode::OK);
    if (!primary_ok) {
        runtime_status_.primary_read_failures++;
        ESP_LOGW(TAG, "Primary config read failed (%s/%d)",
                 common::error_code_to_string(primary_read), static_cast<int>(primary_read));
    }

    AppConfig backup{};
    const common::ErrorCode backup_read = read_blob_from_nvs(handle, kNvsBackupKey, backup);
    const bool backup_ok = (backup_read == common::ErrorCode::OK);
    if (!backup_ok) {
        runtime_status_.backup_read_failures++;
        ESP_LOGW(TAG, "Backup config read failed (%s/%d)",
                 common::error_code_to_string(backup_read), static_cast<int>(backup_read));
    }

    nvs_close(handle);

    if (primary_ok) {
        auto r = apply_loaded_config(primary, ConfigLoadSource::PrimaryNvs);
        if (r.is_ok()) {
            return r;
        }
        runtime_status_.last_load_error = r.error();
        if (!backup_ok) {
            return r;
        }
        ESP_LOGW(TAG, "Primary config rejected (%s/%d), trying backup",
                 common::error_code_to_string(r.error()), static_cast<int>(r.error()));
    }

    if (backup_ok) {
        auto r = apply_loaded_config(backup, ConfigLoadSource::BackupNvs);
        if (r.is_ok()) {
            ESP_LOGW(TAG, "Loaded config from backup key");
            return r;
        }
        runtime_status_.last_load_error = r.error();
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
        runtime_status_.last_persist_error = common::ErrorCode::NvsOpenFailed;
        return common::Result<void>::error(common::ErrorCode::NvsOpenFailed);
    }

    err = nvs_set_blob(handle, kNvsKey, &cfg, sizeof(AppConfig));
    if (err != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "NVS blob write failed: %d", err);
        runtime_status_.last_persist_error = common::ErrorCode::NvsWriteFailed;
        return common::Result<void>::error(common::ErrorCode::NvsWriteFailed);
    }

    err = nvs_set_blob(handle, kNvsBackupKey, &cfg, sizeof(AppConfig));
    if (err != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "NVS backup blob write failed: %d", err);
        runtime_status_.last_persist_error = common::ErrorCode::NvsWriteFailed;
        return common::Result<void>::error(common::ErrorCode::NvsWriteFailed);
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit failed: %d", err);
        runtime_status_.last_persist_error = common::ErrorCode::NvsWriteFailed;
        return common::Result<void>::error(common::ErrorCode::NvsWriteFailed);
    }

    runtime_status_.last_persist_error = common::ErrorCode::OK;
    ESP_LOGI(TAG, "Config persisted to NVS + backup (version %lu)",
             static_cast<unsigned long>(cfg.version));
    return common::Result<void>::ok();
#endif
}

} // namespace config_store
