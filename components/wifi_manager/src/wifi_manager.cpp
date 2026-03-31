#include "wifi_manager/wifi_manager.hpp"
#include "config_store/config_store.hpp"
#include <cstring>

#ifndef HOST_TEST_BUILD
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "event_bus/event_bus.hpp"

static const char* TAG = "wifi_mgr";
#endif

namespace wifi_manager {

#ifndef HOST_TEST_BUILD
namespace {
common::Result<void> log_wifi_error(const char* step, esp_err_t err, common::ErrorCode code) {
    ESP_LOGE(TAG, "%s failed: %d", step, static_cast<int>(err));
    return common::Result<void>::error(code);
}
} // namespace
#endif

WifiManager& WifiManager::instance() {
    static WifiManager mgr;
    return mgr;
}

common::Result<void> WifiManager::initialize() {
    if (initialized_) {
        return common::Result<void>::error(common::ErrorCode::AlreadyInitialized);
    }

#ifndef HOST_TEST_BUILD
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return log_wifi_error("esp_netif_init", err, common::ErrorCode::Unknown);
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return log_wifi_error("esp_event_loop_create_default", err, common::ErrorCode::Unknown);
    }

    if (!esp_netif_create_default_wifi_sta()) {
        return log_wifi_error("esp_netif_create_default_wifi_sta", ESP_FAIL,
                              common::ErrorCode::Unknown);
    }
    if (!esp_netif_create_default_wifi_ap()) {
        return log_wifi_error("esp_netif_create_default_wifi_ap", ESP_FAIL,
                              common::ErrorCode::Unknown);
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        return log_wifi_error("esp_wifi_init", err, common::ErrorCode::Unknown);
    }

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler,
                                              this, nullptr);
    if (err != ESP_OK) {
        return log_wifi_error("esp_event_handler_instance_register(WIFI_EVENT)", err,
                              common::ErrorCode::Unknown);
    }
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler,
                                              this, nullptr);
    if (err != ESP_OK) {
        return log_wifi_error("esp_event_handler_instance_register(IP_EVENT)", err,
                              common::ErrorCode::Unknown);
    }

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        return log_wifi_error("esp_wifi_set_storage", err, common::ErrorCode::Unknown);
    }
#endif

    state_ = WifiState::Disconnected;
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

#ifdef HOST_TEST_BUILD
    (void)password;
#endif

    std::strncpy(current_ssid_, ssid, sizeof(current_ssid_) - 1);
    current_ssid_[sizeof(current_ssid_) - 1] = '\0';
    max_retries_ = config_store::ConfigStore::instance().config().wifi.max_retries;
    retry_count_ = 0;
    retry_exhausted_at_us_ = 0;

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

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return log_wifi_error("esp_wifi_set_mode(STA)", err, common::ErrorCode::WifiConnectFailed);
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        return log_wifi_error("esp_wifi_set_config(STA)", err,
                              common::ErrorCode::WifiConnectFailed);
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        return log_wifi_error("esp_wifi_start(STA)", err, common::ErrorCode::WifiConnectFailed);
    }

    ESP_LOGI(TAG, "WiFi STA starting, SSID: %s", current_ssid_);
#endif

    state_ = WifiState::Connecting;
    return common::Result<void>::ok();
}

common::Result<void> WifiManager::start_ap(const char* ap_ssid) {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    if (!ap_ssid || ap_ssid[0] == '\0') {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }

    std::strncpy(current_ssid_, ap_ssid, sizeof(current_ssid_) - 1);
    current_ssid_[sizeof(current_ssid_) - 1] = '\0';
    retry_count_ = 0;
    retry_exhausted_at_us_ = 0;

#ifndef HOST_TEST_BUILD
    wifi_config_t wifi_config{};
    std::strncpy(reinterpret_cast<char*>(wifi_config.ap.ssid), ap_ssid,
                 sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid_len = static_cast<uint8_t>(std::strlen(ap_ssid));
    wifi_config.ap.channel = 1;
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    wifi_config.ap.max_connection = 2;

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        return log_wifi_error("esp_wifi_set_mode(AP)", err, common::ErrorCode::WifiApStartFailed);
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (err != ESP_OK) {
        return log_wifi_error("esp_wifi_set_config(AP)", err,
                              common::ErrorCode::WifiApStartFailed);
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        return log_wifi_error("esp_wifi_start(AP)", err, common::ErrorCode::WifiApStartFailed);
    }

    ESP_LOGI(TAG, "WiFi AP started, SSID: %s", ap_ssid);
#endif

    state_ = WifiState::ApMode;
    std::strncpy(ip_address_, "192.168.4.1", sizeof(ip_address_) - 1);
    return common::Result<void>::ok();
}

common::Result<void> WifiManager::stop() {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }

#ifndef HOST_TEST_BUILD
    esp_wifi_stop();
#endif

    state_ = WifiState::Disconnected;
    ip_address_[0] = '\0';
    current_ssid_[0] = '\0';
    retry_count_ = 0;
    retry_exhausted_at_us_ = 0;
    return common::Result<void>::ok();
}

void WifiManager::poll_retry_timer() {
#ifndef HOST_TEST_BUILD
    if (!initialized_ || state_ != WifiState::Disconnected || retry_count_ < max_retries_ ||
        retry_exhausted_at_us_ == 0) {
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    if ((now_us - retry_exhausted_at_us_) < kRetryBackoffUs) {
        return;
    }

    retry_count_ = 0;
    retry_exhausted_at_us_ = 0;
    reconnect_count_++;
    ESP_LOGW(TAG, "WiFi retry backoff elapsed, restarting STA connect attempts");
    esp_wifi_connect();
    state_ = WifiState::Connecting;
#endif
}

WifiStatus WifiManager::status() const {
    WifiStatus s{};
    s.state = state_;
    std::strncpy(s.ip_address, ip_address_, sizeof(s.ip_address) - 1);
    s.rssi_dbm = rssi_dbm_;
    s.reconnect_count = reconnect_count_;
    std::strncpy(s.ssid, current_ssid_, sizeof(s.ssid) - 1);
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
        esp_wifi_connect();
        break;

    case WIFI_EVENT_STA_DISCONNECTED:
        state_ = WifiState::Disconnected;
        ip_address_[0] = '\0';
        rssi_dbm_ = 0;

        event_bus::EventBus::instance().publish(event_bus::EventType::WifiDisconnected);

        if (retry_count_ < max_retries_) {
            retry_count_++;
            reconnect_count_++;
            ESP_LOGW(TAG, "WiFi disconnected, retry %u/%u", retry_count_, max_retries_);
            esp_wifi_connect();
            state_ = WifiState::Connecting;
        } else {
            ESP_LOGE(TAG, "WiFi max retries (%u) reached", max_retries_);
            if (retry_exhausted_at_us_ == 0) {
                retry_exhausted_at_us_ = esp_timer_get_time();
            }
        }
        break;

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
        snprintf(ip_address_, sizeof(ip_address_), IPSTR, IP2STR(&event->ip_info.ip));
        state_ = WifiState::Connected;
        retry_count_ = 0;
        retry_exhausted_at_us_ = 0;

        // Query current RSSI
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            rssi_dbm_ = ap_info.rssi;
        }

        ESP_LOGI(TAG, "WiFi connected, IP: %s, RSSI: %d dBm", ip_address_, rssi_dbm_);

        event_bus::EventBus::instance().publish(event_bus::EventType::WifiConnected);
    }
}

#endif // HOST_TEST_BUILD

} // namespace wifi_manager
