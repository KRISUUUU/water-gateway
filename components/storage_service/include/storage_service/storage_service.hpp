#pragma once

#include "common/result.hpp"
#include <cstdint>
#include <string>

namespace storage_service {

class StorageService {
  public:
    static StorageService& instance();

    // Mount SPIFFS partition. Call once during boot.
    common::Result<void> initialize();

    // Read a file from SPIFFS. Returns file contents as string.
    common::Result<std::string> read_file(const char* path);

    // Write content to a file on SPIFFS.
    common::Result<void> write_file(const char* path, const char* content, size_t length);

    // Check if a file exists on SPIFFS.
    bool file_exists(const char* path);

    // Get total and used bytes on SPIFFS partition.
    struct SpaceInfo {
        size_t total_bytes;
        size_t used_bytes;
    };
    SpaceInfo space_info() const;

    bool is_mounted() const {
        return mounted_;
    }

  private:
    StorageService() = default;

    bool mounted_ = false;
};

} // namespace storage_service
