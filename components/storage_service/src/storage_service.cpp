#include "storage_service/storage_service.hpp"

namespace storage_service {

StorageService& StorageService::instance() {
    static StorageService service;
    return service;
}

common::Result<void> StorageService::initialize() {
    if (initialized_) {
        return common::Result<void>(common::ErrorCode::AlreadyInitialized);
    }

    initialized_ = true;
    return common::Result<void>();
}

common::Result<void> StorageService::write_text_file(const std::string& path, const std::string& content) {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    if (path.empty()) {
        return common::Result<void>(common::ErrorCode::InvalidArgument);
    }

    (void)content;
    return common::Result<void>();
}

common::Result<std::string> StorageService::read_text_file(const std::string& path) {
    if (!initialized_) {
        return common::Result<std::string>(common::ErrorCode::NotInitialized);
    }

    if (path.empty()) {
        return common::Result<std::string>(common::ErrorCode::InvalidArgument);
    }

    return common::Result<std::string>(std::string{});
}

}  // namespace storage_service
