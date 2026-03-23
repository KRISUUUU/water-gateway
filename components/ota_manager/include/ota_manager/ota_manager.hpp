#pragma once

#include <string>

#include "common/result.hpp"
#include "ota_manager/ota_state.hpp"

namespace ota_manager {

class OtaManager {
public:
    static OtaManager& instance();

    common::Result<void> initialize();
    common::Result<void> begin_from_url(const std::string& url);
    common::Result<void> begin_from_upload();
    common::Result<void> mark_boot_successful();

    [[nodiscard]] OtaStatus status() const;

private:
    OtaManager() = default;

    bool initialized_{false};
    OtaStatus status_{};
};

}  // namespace ota_manager
