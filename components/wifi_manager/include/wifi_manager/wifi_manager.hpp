#pragma once

#include "common/error.hpp"
#include "common/result.hpp"
#include <atomic>
#include <cstdint>
#include <mutex>

#ifndef HOST_TEST_BUILD
#include "esp_event_base.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
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

    WifiStatus status() const;
    WifiState state() const {
        return state_.load();
    }

  private:
    WifiManager() = default;

#ifndef HOST_TEST_BUILD
    static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id,
                                   void* event_data);
    static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id,
                                 void* event_data);
    void handle_wifi_event(int32_t event_id, void* event_data);
    void handle_ip_event(int32_t event_id, void* event_data);

    // Timer callback that fires after backoff delay to attempt reconnect.
    static void reconnect_timer_cb(TimerHandle_t xTimer);

    void* reconnect_timer_ = nullptr; // TimerHandle_t (opaque to keep C++ semantics clean)
#endif

    bool initialized_ = false;

    // Fields read from health_task (Core0) and written from WiFi/IP event loop
    // tasks — must be atomic or mutex-protected.
    std::atomic<WifiState> state_{WifiState::Uninitialized};
    std::atomic<int8_t> rssi_dbm_{0};
    std::atomic<uint32_t> reconnect_count_{0};

    // Exponential backoff state for STA reconnection (W1 fix).
    // Written from the WiFi event loop and reset from start_sta() / handle_ip_event().
    std::atomic<uint32_t> backoff_ms_{kMinBackoffMs};

    // Mutex protects char array fields (ip_address_, current_ssid_) which cannot
    // be updated or read atomically because they are multi-byte strings (W2 fix).
    mutable std::mutex mutex_{};
    char ip_address_[16] = {};
    char current_ssid_[33] = {};

    // Backoff constants
    static constexpr uint32_t kMinBackoffMs = 2000;
    static constexpr uint32_t kMaxBackoffMs = 120000;
};

} // namespace wifi_manager
