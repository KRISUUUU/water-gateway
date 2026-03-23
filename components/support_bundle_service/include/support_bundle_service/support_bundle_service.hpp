#pragma once

#include <string>

namespace support_bundle_service {

class SupportBundleService {
public:
    static SupportBundleService& instance();

    [[nodiscard]] std::string generate_bundle_json() const;

private:
    SupportBundleService() = default;
};

}  // namespace support_bundle_service
