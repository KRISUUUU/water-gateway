#include "storage_service/storage_service.hpp"

#ifndef HOST_TEST_BUILD
#include "esp_log.h"
#include "esp_spiffs.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

static const char* TAG = "storage_svc";
static const char* MOUNT_POINT = "/storage";
#endif

namespace storage_service {

StorageService& StorageService::instance() {
    static StorageService svc;
    return svc;
}

common::Result<void> StorageService::initialize() {
    if (mounted_) {
        return common::Result<void>::error(common::ErrorCode::AlreadyInitialized);
    }

#ifndef HOST_TEST_BUILD
    esp_vfs_spiffs_conf_t conf{};
    conf.base_path = MOUNT_POINT;
    conf.partition_label = "storage";
    conf.max_files = 8;
    conf.format_if_mount_failed = true;

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %d", err);
        return common::Result<void>::error(common::ErrorCode::StorageNotMounted);
    }

    size_t total = 0, used = 0;
    esp_spiffs_info("storage", &total, &used);
    ESP_LOGI(TAG, "SPIFFS mounted: %zu total, %zu used", total, used);
#endif

    mounted_ = true;
    return common::Result<void>::ok();
}

common::Result<std::string> StorageService::read_file(const char* path) {
    if (!mounted_) {
        return common::Result<std::string>::error(common::ErrorCode::StorageNotMounted);
    }
    if (!path || path[0] == '\0') {
        return common::Result<std::string>::error(common::ErrorCode::InvalidArgument);
    }

#ifndef HOST_TEST_BUILD
    char full_path[128];
    std::snprintf(full_path, sizeof(full_path), "%s/%s", MOUNT_POINT, path);

    FILE* f = std::fopen(full_path, "r");
    if (!f) {
        return common::Result<std::string>::error(common::ErrorCode::StorageReadFailed);
    }

    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 512 * 1024) {
        std::fclose(f);
        return common::Result<std::string>::error(common::ErrorCode::StorageReadFailed);
    }

    std::string content(static_cast<size_t>(size), '\0');
    size_t read = std::fread(&content[0], 1, static_cast<size_t>(size), f);
    std::fclose(f);

    content.resize(read);
    return common::Result<std::string>::ok(std::move(content));
#else
    (void)path;
    return common::Result<std::string>::error(common::ErrorCode::StorageReadFailed);
#endif
}

common::Result<void> StorageService::write_file(const char* path, const char* content,
                                                size_t length) {
    if (!mounted_) {
        return common::Result<void>::error(common::ErrorCode::StorageNotMounted);
    }
    if (!path || !content) {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }

#ifndef HOST_TEST_BUILD
    char full_path[128];
    std::snprintf(full_path, sizeof(full_path), "%s/%s", MOUNT_POINT, path);

    FILE* f = std::fopen(full_path, "w");
    if (!f) {
        return common::Result<void>::error(common::ErrorCode::StorageWriteFailed);
    }

    size_t written = std::fwrite(content, 1, length, f);
    std::fclose(f);

    if (written != length) {
        return common::Result<void>::error(common::ErrorCode::StorageWriteFailed);
    }

    return common::Result<void>::ok();
#else
    (void)path;
    (void)content;
    (void)length;
    return common::Result<void>::ok();
#endif
}

bool StorageService::file_exists(const char* path) {
    if (!mounted_ || !path)
        return false;

#ifndef HOST_TEST_BUILD
    char full_path[128];
    std::snprintf(full_path, sizeof(full_path), "%s/%s", MOUNT_POINT, path);

    struct stat st;
    return stat(full_path, &st) == 0;
#else
    (void)path;
    return false;
#endif
}

StorageService::SpaceInfo StorageService::space_info() const {
    SpaceInfo info{0, 0};
#ifndef HOST_TEST_BUILD
    if (mounted_) {
        esp_spiffs_info("storage", &info.total_bytes, &info.used_bytes);
    }
#endif
    return info;
}

} // namespace storage_service
