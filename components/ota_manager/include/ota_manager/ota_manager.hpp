#pragma once

#include "common/result.hpp"
#include "ota_manager/ota_state.hpp"
#include <cstddef>
#include <cstdint>

namespace ota_manager {

class OtaManager {
  public:
    static OtaManager& instance();

    common::Result<void> initialize();

    // Begin a streamed upload OTA. Call write_chunk() repeatedly, then finalize().
    common::Result<void> begin_upload(size_t image_size);
    common::Result<void> write_chunk(const uint8_t* data, size_t length);
    common::Result<void> finalize_upload();
    common::Result<void> abort_upload();

    // Begin URL-based OTA download. Blocks until complete or fails.
    common::Result<void> begin_url_ota(const char* url);

    // Mark current firmware as valid (cancels rollback timer).
    // Should be called once boot health checks pass.
    common::Result<void> mark_boot_valid();

    OtaStatus status() const;

  private:
    OtaManager() = default;

    void set_status(OtaState state, const char* msg, uint8_t progress = 0);

    bool initialized_ = false;
    OtaStatus status_{};

#ifndef HOST_TEST_BUILD
    void reset_upload_state(bool abort_active = true);
    void* update_handle_ = nullptr;    // esp_ota_handle_t
    void* update_partition_ = nullptr; // const esp_partition_t*
    size_t bytes_written_ = 0;
    size_t image_size_ = 0;
#endif
};

} // namespace ota_manager
