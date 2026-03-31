#pragma once

#include "common/error.hpp"
#include "common/result.hpp"
#include <cstdint>

#ifndef HOST_TEST_BUILD
#include "esp_event_base.h"
#endif

namespace wifi_manager {

enum class WifiState : uint8_t {
    Uninitialized = 0,
    Disconnected,
    Connecting,
    Connected,
    ApMode, // SoftAP for provisioning
};

struct WifiStatus {
    WifiState state;
    char ip_address[16];
    int8_t rssi_dbm;
    uint32_t reconnect_count;
    char ssid[33];
};

class WifiManager {
  public:
    static WifiManager& instance();

    // Initialize WiFi subsystem (must be called once)
    common::Result<void> initialize();

    // Connect to configured STA network
    common::Result<void> start_sta(const char* ssid, const char* password);

    // Start SoftAP for provisioning
    common::Result<void> start_ap(const char* ap_ssid);

    // Stop WiFi (STA or AP)
    common::Result<void> stop();

    // Poll non-blocking reconnect timers for the STA path.
    void poll_retry_timer();

    WifiStatus status() const;
    WifiState state() const {
        return state_;
    }

#ifdef HOST_TEST_BUILD
    uint8_t configured_max_retries_for_test() const {
        return max_retries_;
    }
#endif

  private:
    WifiManager() = default;

#ifndef HOST_TEST_BUILD
    static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id,
                                   void* event_data);
    static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id,
                                 void* event_data);
    void handle_wifi_event(int32_t event_id, void* event_data);
    void handle_ip_event(int32_t event_id, void* event_data);
#endif

    bool initialized_ = false;
    WifiState state_ = WifiState::Uninitialized;
    char ip_address_[16] = {};
    int8_t rssi_dbm_ = 0;
    uint32_t reconnect_count_ = 0;
    char current_ssid_[33] = {};
    uint8_t retry_count_ = 0;
    uint8_t max_retries_ = 10;
    int64_t retry_exhausted_at_us_ = 0;
    static constexpr int64_t kRetryBackoffUs = 5LL * 60LL * 1000000LL;
};

} // namespace wifi_manager
