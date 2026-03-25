#pragma once

#include "common/result.hpp"
#include <cstdint>

#ifndef HOST_TEST_BUILD
#include <sys/time.h>
#endif

namespace ntp_service {

struct NtpStatus {
    bool synchronized;
    int64_t last_sync_epoch_s;
};

class NtpService {
  public:
    static NtpService& instance();

    common::Result<void> initialize();
    common::Result<void> start();
    common::Result<void> stop();

    NtpStatus status() const;
    bool is_synchronized() const {
        return synchronized_;
    }

    // Returns current UTC time as epoch seconds, or 0 if not synchronized
    int64_t now_epoch_s() const;

    // Returns current UTC time as epoch milliseconds, or 0 if not synchronized
    int64_t now_epoch_ms() const;

  private:
    NtpService() = default;

#ifndef HOST_TEST_BUILD
    static void time_sync_notification_cb(struct ::timeval* tv);
#endif

    bool initialized_ = false;
    bool started_ = false;
    bool synchronized_ = false;
    int64_t last_sync_epoch_s_ = 0;
};

} // namespace ntp_service
