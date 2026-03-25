#pragma once

#include "common/result.hpp"
#include <string>

namespace support_bundle_service {

class SupportBundleService {
  public:
    static SupportBundleService& instance();

    /// JSON bundle: diagnostics snapshot, metrics, health, redacted config, log lines.
    [[nodiscard]] common::Result<std::string> generate_bundle_json() const;

  private:
    SupportBundleService() = default;
};

} // namespace support_bundle_service
