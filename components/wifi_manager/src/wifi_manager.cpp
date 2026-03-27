#include "wifi_manager/wifi_manager.hpp"

#ifndef HOST_TEST_BUILD
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "event_bus/event_bus.hpp"
#include <algorithm>
#include <cstring>

static const char* TAG = "wifi_mgr";
#endif

namespace wifi_manager {

WifiManager& WifiManager::instance() {
    static WifiManager mgr;
    return mgr;
}

common::Result<void> WifiManager::initialize() {
    if (initialized_) {
        return common::Result<void>::error(common::ErrorCode::AlreadyInitialized);
    }

#ifndef HOST_TEST_BUILD
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, this, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &ip_event_handler, this, nullptr));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    // Create the one-shot reconnect timer. The timer fires after the current
    // backoff period and calls esp_wifi_connect() to attempt a new connection.
    // Using this timer avoids blocking the WiFi event loop task with vTaskDelay.
    reconnect_timer_ = xTimerCreate(
        "wifi_retry",
        pdMS_TO_TICKS(kMinBackoffMs),
        pdFALSE,   // one-shot (not auto-reload)
        this,      // timer ID carries the WifiManager* for the callback
        reconnect_timer_cb
    );
    if (!reconnect_timer_) {
        ESP_LOGE(TAG, "Failed to create WiFi reconnect timer — retries will be immediate");
    }
#endif

    state_.store(WifiState::Disconnected);
    initialized_ = true;
    return common::Result<void>::ok();
}

common::Result<void> WifiManager::start_sta(const char* ssid, const char* password) {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    if (!ssid || ssid[0] == '\0') {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::strncpy(current_ssid_, ssid, sizeof(current_ssid_) - 1);
        current_ssid_[sizeof(current_ssid_) - 1] = '\0';
    }

    // Reset backoff whenever a fresh connection attempt is initiated by the
    // application layer (e.g., after reconfiguration).
    backoff_ms_.store(kMinBackoffMs);

#ifndef HOST_TEST_BUILD
    wifi_config_t wifi_config{};
    std::strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), ssid,
                 sizeof(wifi_config.sta.ssid) - 1);
    if (password && password[0] != '\0') {
        std::strncpy(reinterpret_cast<char*>(wifi_config.sta.password), password,
                     sizeof(wifi_config.sta.password) - 1);
    }
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    {
        std::lock_guard<std::mutex> lock(mutex_);
        ESP_LOGI(TAG, "WiFi STA starting, SSID: %s", current_ssid_);
    }
#endif

    state_.store(WifiState::Connecting);
    return common::Result<void>::ok();
}

common::Result<void> WifiManager::start_ap(const char* ap_ssid) {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    if (!ap_ssid || ap_ssid[0] == '\0') {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }

#ifndef HOST_TEST_BUILD
    wifi_config_t wifi_config{};
    std::strncpy(reinterpret_cast<char*>(wifi_config.ap.ssid), ap_ssid,
                 sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid_len = static_cast<uint8_t>(std::strlen(ap_ssid));
    wifi_config.ap.channel = 1;
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    wifi_config.ap.max_connection = 2;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started, SSID: %s", ap_ssid);
#endif

    state_.store(WifiState::ApMode);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::strncpy(ip_address_, "192.168.4.1", sizeof(ip_address_) - 1);
        ip_address_[sizeof(ip_address_) - 1] = '\0';
    }
    return common::Result<void>::ok();
}

common::Result<void> WifiManager::stop() {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }

#ifndef HOST_TEST_BUILD
    // Stop the reconnect timer before bringing WiFi down to prevent a stale
    // timer firing and calling esp_wifi_connect() on a stopped interface.
    if (reconnect_timer_) {
        xTimerStop(static_cast<TimerHandle_t>(reconnect_timer_), pdMS_TO_TICKS(100));
    }
    esp_wifi_stop();
#endif

    state_.store(WifiState::Disconnected);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ip_address_[0] = '\0';
    }
    return common::Result<void>::ok();
}

WifiStatus WifiManager::status() const {
    WifiStatus s{};
    // Atomics: single-load reads are always consistent.
    s.state = state_.load();
    s.rssi_dbm = rssi_dbm_.load();
    s.reconnect_count = reconnect_count_.load(std::memory_order_relaxed);
    // Mutex: multi-byte string fields require exclusive access to prevent tearing.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::strncpy(s.ip_address, ip_address_, sizeof(s.ip_address) - 1);
        s.ip_address[sizeof(s.ip_address) - 1] = '\0';
        std::strncpy(s.ssid, current_ssid_, sizeof(s.ssid) - 1);
        s.ssid[sizeof(s.ssid) - 1] = '\0';
    }
    return s;
}

#ifndef HOST_TEST_BUILD

void WifiManager::wifi_event_handler(void* arg, esp_event_base_t /*event_base*/, int32_t event_id,
                                     void* event_data) {
    auto* self = static_cast<WifiManager*>(arg);
    self->handle_wifi_event(event_id, event_data);
}

void WifiManager::ip_event_handler(void* arg, esp_event_base_t /*event_base*/, int32_t event_id,
                                   void* event_data) {
    auto* self = static_cast<WifiManager*>(arg);
    self->handle_ip_event(event_id, event_data);
}

void WifiManager::handle_wifi_event(int32_t event_id, void* /*event_data*/) {
    switch (event_id) {
    case WIFI_EVENT_STA_START:
        // Immediately attempt first connection when the STA interface starts.
        esp_wifi_connect();
        break;

    case WIFI_EVENT_STA_DISCONNECTED: {
        state_.store(WifiState::Disconnected);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ip_address_[0] = '\0';
        }
        rssi_dbm_.store(0);
        reconnect_count_.fetch_add(1, std::memory_order_relaxed);

        // --- W1: Exponential backoff infinite retry (replaces hard retry limit) ---
        // Load current backoff, schedule the timer, then double for next attempt.
        const uint32_t delay_ms = backoff_ms_.load();
        ESP_LOGW(TAG, "WiFi disconnected — retry in %lu ms (exponential backoff)",
                 static_cast<unsigned long>(delay_ms));

        if (reconnect_timer_) {
            // Adjust the period and restart the one-shot timer.
            xTimerChangePeriod(static_cast<TimerHandle_t>(reconnect_timer_),
                               pdMS_TO_TICKS(delay_ms),
                               0 /* don't block if timer queue is full */);
            xTimerStart(static_cast<TimerHandle_t>(reconnect_timer_), 0);
        } else {
            // Timer creation failed at init — fall back to immediate reconnect.
            ESP_LOGW(TAG, "Reconnect timer unavailable, retrying immediately");
            esp_wifi_connect();
            state_.store(WifiState::Connecting);
        }

        // Double the backoff for the next disconnect, capped at kMaxBackoffMs.
        // Guard against overflow: if delay_ms >= kMaxBackoffMs/2 the multiply
        // would overflow uint32_t, so we cap before multiplying.
        const uint32_t next_backoff =
            (delay_ms >= kMaxBackoffMs / 2) ? kMaxBackoffMs : delay_ms * 2;
        backoff_ms_.store(next_backoff);

        event_bus::EventBus::instance().publish(event_bus::EventType::WifiDisconnected);
        break;
    }

    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "WiFi associated with AP");
        break;

    default:
        break;
    }
}

void WifiManager::handle_ip_event(int32_t event_id, void* event_data) {
    if (event_id == IP_EVENT_STA_GOT_IP) {
        auto* event = static_cast<ip_event_got_ip_t*>(event_data);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snprintf(ip_address_, sizeof(ip_address_), IPSTR, IP2STR(&event->ip_info.ip));
        }
        state_.store(WifiState::Connected);

        // Reset backoff to minimum on successful connection so the next
        // disconnect starts fresh rather than with the last long interval.
        backoff_ms_.store(kMinBackoffMs);

        // Query current RSSI
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            rssi_dbm_.store(ap_info.rssi);
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            ESP_LOGI(TAG, "WiFi connected, IP: %s, RSSI: %d dBm", ip_address_,
                     static_cast<int>(rssi_dbm_.load()));
        }

        event_bus::EventBus::instance().publish(event_bus::EventType::WifiConnected);
    }
}

void WifiManager::reconnect_timer_cb(TimerHandle_t xTimer) {
    // Recover the WifiManager instance from the timer ID set in initialize().
    auto* self = static_cast<WifiManager*>(pvTimerGetTimerID(xTimer));
    if (!self || !self->initialized_) {
        return;
    }
    // Do not attempt reconnect if we're in AP mode (provisioning).
    if (self->state_.load() == WifiState::ApMode) {
        return;
    }
    ESP_LOGI(TAG, "WiFi reconnect attempt...");
    self->state_.store(WifiState::Connecting);
    esp_wifi_connect();
}

#endif // HOST_TEST_BUILD

} // namespace wifi_manager
