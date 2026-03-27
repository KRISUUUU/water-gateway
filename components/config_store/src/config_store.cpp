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

    initialized_ = true;

    auto load_result = load_from_nvs();
    if (load_result.is_error()) {
#ifndef HOST_TEST_BUILD
        ESP_LOGW(TAG, "No stored config found, applying defaults");
#endif
        config_ = AppConfig::make_default();
        auto persist_result = persist_to_nvs(config_);
        if (persist_result.is_error()) {
#ifndef HOST_TEST_BUILD
            ESP_LOGE(TAG, "Failed to persist default config");
#endif
            return persist_result;
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

common::Result<ValidationResult> ConfigStore::save(const AppConfig& new_config) {
    if (!initialized_) {
        return common::Result<ValidationResult>::error(common::ErrorCode::NotInitialized);
    }

    // Validate first
    auto validation = validate_config(new_config);
    if (!validation.valid) {
        return common::Result<ValidationResult>::ok(validation);
    }

    // Migrate if needed
    auto migrated = migrate_to_current(new_config);
    if (migrated.is_error()) {
        return common::Result<ValidationResult>::error(migrated.error());
    }

    // C1 fix: update RAM config_ under lock BEFORE the slow NVS write so that
    // concurrent config() readers never observe a state where NVS is already
    // updated but the in-process config_ still holds stale data (TOCTOU).
    // If persist_to_nvs() subsequently fails the runtime config is still correct;
    // only the NVS backing store is stale (resolved on next successful save).
#ifndef HOST_TEST_BUILD
    xSemaphoreTake(static_cast<SemaphoreHandle_t>(mutex_), portMAX_DELAY);
#endif

    config_ = migrated.value();
    loaded_ = true;

#ifndef HOST_TEST_BUILD
    xSemaphoreGive(static_cast<SemaphoreHandle_t>(mutex_));
#endif

    // Persist outside the lock — NVS writes can take ~10–100 ms and must not
    // block config() readers for that duration.
    auto persist_result = persist_to_nvs(migrated.value());
    if (persist_result.is_error()) {
#ifndef HOST_TEST_BUILD
        ESP_LOGE(TAG, "NVS persist failed after RAM update — reboot may revert config");
#endif
        return common::Result<ValidationResult>::error(persist_result.error());
    }

    event_bus::EventBus::instance().publish(event_bus::EventType::ConfigChanged);
    return common::Result<ValidationResult>::ok(validation);
}

common::Result<void> ConfigStore::reset_to_defaults() {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }

    AppConfig defaults = AppConfig::make_default();

    // C1 fix: same RAM-first, then NVS pattern as save().
#ifndef HOST_TEST_BUILD
    xSemaphoreTake(static_cast<SemaphoreHandle_t>(mutex_), portMAX_DELAY);
#endif

    config_ = defaults;
    loaded_ = true;

#ifndef HOST_TEST_BUILD
    xSemaphoreGive(static_cast<SemaphoreHandle_t>(mutex_));
#endif

    auto result = persist_to_nvs(defaults);
    if (result.is_error()) {
#ifndef HOST_TEST_BUILD
        ESP_LOGE(TAG, "NVS persist failed after reset — reboot may not restore defaults");
#endif
        return result;
    }

    event_bus::EventBus::instance().publish(event_bus::EventType::ConfigChanged);
    return common::Result<void>::ok();
}

common::Result<void> ConfigStore::load_from_nvs() {
#ifdef HOST_TEST_BUILD
    // In host test builds, there is no NVS. Return error to trigger defaults.
    return common::Result<void>::error(common::ErrorCode::NvsReadFailed);
#else
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "NVS namespace not found (first boot?)");
        return common::Result<void>::error(common::ErrorCode::NvsOpenFailed);
    }

    size_t required_size = sizeof(AppConfig);
    err = nvs_get_blob(handle, kNvsKey, &config_, &required_size);
    nvs_close(handle);

    if (err != ESP_OK || required_size != sizeof(AppConfig)) {
        ESP_LOGW(TAG, "NVS config read failed or size mismatch");
        return common::Result<void>::error(common::ErrorCode::NvsReadFailed);
    }

    // Migrate if needed
    if (config_.version != kCurrentConfigVersion) {
        ESP_LOGI(TAG, "Config version %lu, current %lu — migrating", (unsigned long)config_.version,
                 (unsigned long)kCurrentConfigVersion);
        auto migrated = migrate_to_current(config_);
        if (migrated.is_error()) {
            ESP_LOGE(TAG, "Config migration failed");
            return common::Result<void>::error(migrated.error());
        }
        config_ = migrated.value();

        // Persist the migrated config
        auto persist_result = persist_to_nvs(config_);
        if (persist_result.is_error()) {
            ESP_LOGW(TAG, "Failed to persist migrated config");
        }
    }

    // Validate loaded config
    auto validation = validate_config(config_);
    if (!validation.valid) {
        ESP_LOGE(TAG, "Stored config is invalid (%zu issues)", validation.issues.size());
        return common::Result<void>::error(common::ErrorCode::ConfigInvalid);
    }
    return common::Result<void>::ok();
#endif
}

common::Result<void> ConfigStore::persist_to_nvs(const AppConfig& cfg) {
#ifdef HOST_TEST_BUILD
    (void)cfg;
    return common::Result<void>::ok();
#else
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open for write failed: %d", err);
        return common::Result<void>::error(common::ErrorCode::NvsOpenFailed);
    }

    err = nvs_set_blob(handle, kNvsKey, &cfg, sizeof(AppConfig));
    if (err != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "NVS blob write failed: %d", err);
        return common::Result<void>::error(common::ErrorCode::NvsWriteFailed);
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit failed: %d", err);
        return common::Result<void>::error(common::ErrorCode::NvsWriteFailed);
    }

    ESP_LOGI(TAG, "Config persisted to NVS (version %lu)", (unsigned long)cfg.version);
    return common::Result<void>::ok();
#endif
}

} // namespace config_store
