#include "ota_manager/ota_manager.hpp"
#include "event_bus/event_bus.hpp"
#include <cstdio>
#include <cstring>

#ifndef HOST_TEST_BUILD
#include "esp_app_format.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
    status_.boot_pending_verify = false;
    status_.boot_marked_valid = false;
    status_.boot_mark_attempts = 0;
    status_.boot_mark_failures = 0;
    status_.last_boot_mark_error = 0;
    std::strncpy(status_.boot_validation_note, "unknown", sizeof(status_.boot_validation_note) - 1);

#ifndef HOST_TEST_BUILD
    const esp_app_desc_t* app_desc = esp_app_get_description();
    if (app_desc) {
        std::strncpy(status_.current_version, app_desc->version,
                     sizeof(status_.current_version) - 1);
    }

    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running) {
        esp_ota_img_states_t img_state{};
        if (esp_ota_get_state_partition(running, &img_state) == ESP_OK) {
            status_.boot_pending_verify = (img_state == ESP_OTA_IMG_PENDING_VERIFY);
            if (status_.boot_pending_verify) {
                std::strncpy(status_.boot_validation_note, "pending_verify",
                             sizeof(status_.boot_validation_note) - 1);
            } else {
                std::strncpy(status_.boot_validation_note, "not_pending_verify",
                             sizeof(status_.boot_validation_note) - 1);
                status_.boot_marked_valid = true;
            }
        } else {
            std::strncpy(status_.boot_validation_note, "state_unavailable",
                         sizeof(status_.boot_validation_note) - 1);
        }
    } else {
        std::strncpy(status_.boot_validation_note, "running_partition_unavailable",
                     sizeof(status_.boot_validation_note) - 1);
    }
#else
    std::strncpy(status_.current_version, "0.1.0", sizeof(status_.current_version) - 1);
    std::strncpy(status_.boot_validation_note, "host_build",
                 sizeof(status_.boot_validation_note) - 1);
    status_.boot_marked_valid = true;
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
    reset_upload_state(true);
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

    ESP_LOGI(TAG, "OTA upload started, partition: %s, size: %zu", partition->label, image_size);
#else
    (void)image_size;
#endif

    set_status(OtaState::InProgress, "Upload in progress");
    event_bus::EventBus::instance().publish(event_bus::EventType::OtaStarted);
    return common::Result<void>::ok();
}

common::Result<void> OtaManager::write_chunk(const uint8_t* data, size_t length) {
    if (status_.state != OtaState::InProgress) {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }
    if (!data || length == 0) {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }

#ifndef HOST_TEST_BUILD
    esp_ota_handle_t handle =
        static_cast<esp_ota_handle_t>(reinterpret_cast<uintptr_t>(update_handle_));
    esp_err_t err = esp_ota_write(handle, data, length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed: %d", err);
        set_status(OtaState::Failed, "Write failed");
        reset_upload_state(true);
        return common::Result<void>::error(common::ErrorCode::OtaWriteFailed);
    }

    bytes_written_ += length;
    if (image_size_ > 0) {
        status_.progress_pct = static_cast<uint8_t>((bytes_written_ * 100) / image_size_);
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
        reset_upload_state(true);
        return common::Result<void>::error(common::ErrorCode::OtaValidationFailed);
    }

    const esp_partition_t* partition = static_cast<const esp_partition_t*>(update_partition_);
    err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %d", err);
        set_status(OtaState::Failed, "Failed to set boot partition");
        reset_upload_state(true);
        return common::Result<void>::error(common::ErrorCode::OtaCommitFailed);
    }

    ESP_LOGI(TAG, "OTA upload finalized, reboot to activate");
    reset_upload_state(false);
#endif

    set_status(OtaState::Rebooting, "OTA complete, reboot to activate", 100);

    event_bus::EventBus::instance().publish(event_bus::EventType::OtaCompleted, 0);

    return common::Result<void>::ok();
}

common::Result<void> OtaManager::abort_upload() {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }

    if (status_.state != OtaState::InProgress && status_.state != OtaState::Validating) {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }

#ifndef HOST_TEST_BUILD
    reset_upload_state(true);
#endif

    set_status(OtaState::Idle, "Upload aborted");
    return common::Result<void>::ok();
}

common::Result<void> OtaManager::begin_url_ota_async(const char* url) {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    if (!url || url[0] == '\0') {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }
    if (status_.state == OtaState::InProgress || status_.state == OtaState::Validating) {
        return common::Result<void>::error(common::ErrorCode::OtaAlreadyInProgress);
    }

#ifndef HOST_TEST_BUILD
    if (url_ota_task_handle_ != nullptr) {
        return common::Result<void>::error(common::ErrorCode::OtaAlreadyInProgress);
    }
    if (std::strlen(url) >= sizeof(pending_url_)) {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }

    std::strncpy(pending_url_, url, sizeof(pending_url_) - 1);
    pending_url_[sizeof(pending_url_) - 1] = '\0';
    set_status(OtaState::InProgress, "Downloading from URL");
    event_bus::EventBus::instance().publish(event_bus::EventType::OtaStarted);

    TaskHandle_t task = nullptr;
    if (xTaskCreatePinnedToCore(&OtaManager::url_ota_task, "ota_url", 8192, this, 4, &task, 0) !=
        pdPASS) {
        pending_url_[0] = '\0';
        set_status(OtaState::Failed, "URL OTA worker start failed");
        event_bus::EventBus::instance().publish(event_bus::EventType::OtaCompleted, -1);
        return common::Result<void>::error(common::ErrorCode::OtaBeginFailed);
    }
    url_ota_task_handle_ = task;
    return common::Result<void>::ok();
#else
    return begin_url_ota(url);
#endif
}

common::Result<void> OtaManager::begin_url_ota(const char* url) {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    if (!url || url[0] == '\0') {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }
    if (status_.state == OtaState::InProgress || status_.state == OtaState::Validating) {
        return common::Result<void>::error(common::ErrorCode::OtaAlreadyInProgress);
    }

    set_status(OtaState::InProgress, "Downloading from URL");
    event_bus::EventBus::instance().publish(event_bus::EventType::OtaStarted);

#ifndef HOST_TEST_BUILD
    return perform_url_ota(url, false);
#else
    set_status(OtaState::Rebooting, "URL OTA complete, reboot to activate", 100);
    event_bus::EventBus::instance().publish(event_bus::EventType::OtaCompleted, 0);
    return common::Result<void>::ok();
#endif
}

#ifndef HOST_TEST_BUILD
common::Result<void> OtaManager::perform_url_ota(const char* url, bool announce_start) {
    if (announce_start) {
        set_status(OtaState::InProgress, "Downloading from URL");
        event_bus::EventBus::instance().publish(event_bus::EventType::OtaStarted);
    }

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
        event_bus::EventBus::instance().publish(event_bus::EventType::OtaCompleted, -1);
        return common::Result<void>::error(common::ErrorCode::OtaWriteFailed);
    }

    ESP_LOGI(TAG, "HTTPS OTA successful, reboot to activate");

    set_status(OtaState::Rebooting, "URL OTA complete, reboot to activate", 100);

    event_bus::EventBus::instance().publish(event_bus::EventType::OtaCompleted, 0);

    return common::Result<void>::ok();
}

void OtaManager::url_ota_task(void* param) {
    auto* self = static_cast<OtaManager*>(param);
    if (self) {
        (void)self->perform_url_ota(self->pending_url_, false);
        self->pending_url_[0] = '\0';
        self->url_ota_task_handle_ = nullptr;
    }
    vTaskDelete(nullptr);
}
#endif

common::Result<void> OtaManager::mark_boot_valid() {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    status_.boot_mark_attempts++;

    if (!status_.boot_pending_verify) {
        status_.boot_marked_valid = true;
        std::strncpy(status_.boot_validation_note, "not_pending_verify",
                     sizeof(status_.boot_validation_note) - 1);
        return common::Result<void>::ok();
    }

#ifndef HOST_TEST_BUILD
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mark app valid: %d", err);
        status_.boot_mark_failures++;
        status_.last_boot_mark_error = static_cast<int32_t>(err);
        std::strncpy(status_.boot_validation_note, "mark_failed",
                     sizeof(status_.boot_validation_note) - 1);
        return common::Result<void>::error(common::ErrorCode::Unknown);
    }
    ESP_LOGI(TAG, "App marked as valid, rollback cancelled");
    status_.boot_pending_verify = false;
    status_.boot_marked_valid = true;
    status_.last_boot_mark_error = 0;
    std::strncpy(status_.boot_validation_note, "marked_valid",
                 sizeof(status_.boot_validation_note) - 1);
#else
    status_.boot_pending_verify = false;
    status_.boot_marked_valid = true;
    status_.last_boot_mark_error = 0;
    std::strncpy(status_.boot_validation_note, "marked_valid_host",
                 sizeof(status_.boot_validation_note) - 1);
#endif
    return common::Result<void>::ok();
}

OtaStatus OtaManager::status() const {
    return status_;
}

void OtaManager::set_status(OtaState state, const char* msg, uint8_t progress) {
    status_.state = state;
    status_.progress_pct = progress;
    if (msg) {
        std::strncpy(status_.message, msg, sizeof(status_.message) - 1);
        status_.message[sizeof(status_.message) - 1] = '\0';
    }
}

#ifndef HOST_TEST_BUILD
void OtaManager::reset_upload_state(bool abort_active) {
    if (abort_active && update_handle_ != nullptr) {
        esp_ota_handle_t handle =
            static_cast<esp_ota_handle_t>(reinterpret_cast<uintptr_t>(update_handle_));
        esp_ota_abort(handle);
    }
    update_handle_ = nullptr;
    update_partition_ = nullptr;
    bytes_written_ = 0;
    image_size_ = 0;
}
#endif

} // namespace ota_manager
