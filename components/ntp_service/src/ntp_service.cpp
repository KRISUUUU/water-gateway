#include "ntp_service/ntp_service.hpp"

#ifndef HOST_TEST_BUILD
#include "esp_log.h"
#include "esp_sntp.h"
#include <ctime>
#include <sys/time.h>

static const char* TAG = "ntp_svc";
#endif

namespace ntp_service {

NtpService& NtpService::instance() {
    static NtpService svc;
    return svc;
}

common::Result<void> NtpService::initialize() {
    if (initialized_) {
        return common::Result<void>::error(common::ErrorCode::AlreadyInitialized);
    }
    initialized_ = true;
    return common::Result<void>::ok();
}

common::Result<void> NtpService::start() {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    if (started_) {
        return common::Result<void>::ok();
    }

#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "Starting SNTP synchronization");

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_set_sync_interval(3600000); // Re-sync every hour
    esp_sntp_init();
#endif

    started_ = true;
    return common::Result<void>::ok();
}

common::Result<void> NtpService::stop() {
    if (!started_) {
        return common::Result<void>::ok();
    }

#ifndef HOST_TEST_BUILD
    esp_sntp_stop();
    ESP_LOGI(TAG, "SNTP stopped");
#endif

    started_ = false;
    return common::Result<void>::ok();
}

NtpStatus NtpService::status() const {
    return {synchronized_, last_sync_epoch_s_};
}

int64_t NtpService::now_epoch_s() const {
#ifndef HOST_TEST_BUILD
    if (!synchronized_) return 0;
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<int64_t>(tv.tv_sec);
#else
    return 0;
#endif
}

int64_t NtpService::now_epoch_ms() const {
#ifndef HOST_TEST_BUILD
    if (!synchronized_) return 0;
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<int64_t>(tv.tv_sec) * 1000 +
           static_cast<int64_t>(tv.tv_usec) / 1000;
#else
    return 0;
#endif
}

#ifndef HOST_TEST_BUILD
void NtpService::time_sync_notification_cb(struct ::timeval* tv) {
    ESP_LOGI(TAG, "NTP time synchronized");
    auto& svc = NtpService::instance();
    svc.synchronized_ = true;
    if (tv) {
        svc.last_sync_epoch_s_ = static_cast<int64_t>(tv->tv_sec);
    }
}
#endif

} // namespace ntp_service
