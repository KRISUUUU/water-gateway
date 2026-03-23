#pragma once

#include "common/result.hpp"

namespace ntp_service {

class NtpService {
public:
    static NtpService& instance();

    common::Result<void> initialize();
    common::Result<void> sync();

private:
    NtpService() = default;
    bool initialized_{false};
};

}  // namespace ntp_service
