#pragma once

#include <string>

#include "common/result.hpp"

namespace storage_service {

class StorageService {
public:
    static StorageService& instance();

    common::Result<void> initialize();
    common::Result<void> write_text_file(const std::string& path, const std::string& content);
    common::Result<std::string> read_text_file(const std::string& path);

private:
    StorageService() = default;
    bool initialized_{false};
};

}  // namespace storage_service
