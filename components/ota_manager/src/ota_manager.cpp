#include "ota_manager/ota_manager.hpp"
#include "event_bus/event_bus.hpp"
#include <cstring>
#include <cstdio>

#ifndef HOST_TEST_BUILD
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_app_format.h"
static const char* TAG = "ota_mgr";
#endif

namespace ota_manager {

OtaManager& OtaManager::instance() {
    static OtaManager mgr;
    return mgr;
}

common::Result<void> OtaManager::initialize() {
    if (initialized_) {
        return common::Result<void>::error(common::ErrorCode::AlreadyInitialized);
    }

    status_.state = OtaState::Idle;

#ifndef HOST_TEST_BUILD
    const esp_app_desc_t* app_desc = esp_app_get_description();
    if (app_desc) {
        std::strncpy(status_.current_version, app_desc->version,
                     sizeof(status_.current_version) - 1);
    }
#else
    std::strncpy(status_.current_version, "0.1.0",
                 sizeof(status_.current_version) - 1);
#endif

    initialized_ = true;
    return common::Result<void>::ok();
}

common::Result<void> OtaManager::begin_upload(size_t image_size) {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    if (status_.state == OtaState::InProgress) {
        return common::Result<void>::error(common::ErrorCode::OtaAlreadyInProgress);
    }

#ifndef HOST_TEST_BUILD
    const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
    if (!partition) {
        set_status(OtaState::Failed, "No OTA partition available");
        return common::Result<void>::error(common::ErrorCode::OtaBeginFailed);
    }

    if (image_size > partition->size) {
        set_status(OtaState::Failed, "Image too large for partition");
        return common::Result<void>::error(common::ErrorCode::OtaImageTooLarge);
    }

    esp_ota_handle_t handle;
    esp_err_t err = esp_ota_begin(partition, image_size, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %d", err);
        set_status(OtaState::Failed, "OTA begin failed");
        return common::Result<void>::error(common::ErrorCode::OtaBeginFailed);
    }

    update_handle_ = reinterpret_cast<void*>(static_cast<uintptr_t>(handle));
    update_partition_ = const_cast<void*>(static_cast<const void*>(partition));
    bytes_written_ = 0;
    image_size_ = image_size;

    ESP_LOGI(TAG, "OTA upload started, partition: %s, size: %zu",
             partition->label, image_size);
#endif

    set_status(OtaState::InProgress, "Upload in progress");
    event_bus::EventBus::instance().publish(event_bus::EventType::OtaStarted);
    return common::Result<void>::ok();
}

common::Result<void> OtaManager::write_chunk(const uint8_t* data,
                                              size_t length) {
    if (status_.state != OtaState::InProgress) {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }

#ifndef HOST_TEST_BUILD
    esp_ota_handle_t handle =
        static_cast<esp_ota_handle_t>(reinterpret_cast<uintptr_t>(update_handle_));
    esp_err_t err = esp_ota_write(handle, data, length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed: %d", err);
        set_status(OtaState::Failed, "Write failed");
        return common::Result<void>::error(common::ErrorCode::OtaWriteFailed);
    }

    bytes_written_ += length;
    if (image_size_ > 0) {
        status_.progress_pct =
            static_cast<uint8_t>((bytes_written_ * 100) / image_size_);
    }
#else
    (void)data;
    (void)length;
#endif

    return common::Result<void>::ok();
}

common::Result<void> OtaManager::finalize_upload() {
    if (status_.state != OtaState::InProgress) {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }

    set_status(OtaState::Validating, "Validating image");

#ifndef HOST_TEST_BUILD
    esp_ota_handle_t handle =
        static_cast<esp_ota_handle_t>(reinterpret_cast<uintptr_t>(update_handle_));

    esp_err_t err = esp_ota_end(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed (validation): %d", err);
        set_status(OtaState::Failed, "Image validation failed");
        return common::Result<void>::error(
            common::ErrorCode::OtaValidationFailed);
    }

    const esp_partition_t* partition =
        static_cast<const esp_partition_t*>(update_partition_);
    err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %d", err);
        set_status(OtaState::Failed, "Failed to set boot partition");
        return common::Result<void>::error(common::ErrorCode::OtaCommitFailed);
    }

    ESP_LOGI(TAG, "OTA upload finalized, reboot to activate");
#endif

    set_status(OtaState::Rebooting, "OTA complete, reboot to activate");
    status_.progress_pct = 100;

    event_bus::EventBus::instance().publish(
        event_bus::EventType::OtaCompleted, 0);

    return common::Result<void>::ok();
}

common::Result<void> OtaManager::begin_url_ota(const char* url) {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    if (!url || url[0] == '\0') {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }
    if (status_.state == OtaState::InProgress) {
        return common::Result<void>::error(common::ErrorCode::OtaAlreadyInProgress);
    }

    set_status(OtaState::InProgress, "Downloading from URL");
    event_bus::EventBus::instance().publish(event_bus::EventType::OtaStarted);

#ifndef HOST_TEST_BUILD
    esp_http_client_config_t http_config{};
    http_config.url = url;
    http_config.timeout_ms = 30000;

    esp_https_ota_config_t ota_config{};
    ota_config.http_config = &http_config;

    ESP_LOGI(TAG, "Starting HTTPS OTA from: %s", url);
    esp_err_t err = esp_https_ota(&ota_config);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTPS OTA failed: %d", err);
        set_status(OtaState::Failed, "URL OTA download failed");
        event_bus::EventBus::instance().publish(
            event_bus::EventType::OtaCompleted, -1);
        return common::Result<void>::error(common::ErrorCode::OtaWriteFailed);
    }

    ESP_LOGI(TAG, "HTTPS OTA successful, reboot to activate");
#endif

    set_status(OtaState::Rebooting, "URL OTA complete, reboot to activate");
    status_.progress_pct = 100;

    event_bus::EventBus::instance().publish(
        event_bus::EventType::OtaCompleted, 0);

    return common::Result<void>::ok();
}

common::Result<void> OtaManager::mark_boot_valid() {
#ifndef HOST_TEST_BUILD
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mark app valid: %d", err);
        return common::Result<void>::error(common::ErrorCode::Unknown);
    }
    ESP_LOGI(TAG, "App marked as valid, rollback cancelled");
#endif
    return common::Result<void>::ok();
}

OtaStatus OtaManager::status() const {
    return status_;
}

void OtaManager::set_status(OtaState state, const char* msg,
                             uint8_t progress) {
    status_.state = state;
    status_.progress_pct = progress;
    if (msg) {
        std::strncpy(status_.message, msg, sizeof(status_.message) - 1);
        status_.message[sizeof(status_.message) - 1] = '\0';
    }
}

} // namespace ota_manager
