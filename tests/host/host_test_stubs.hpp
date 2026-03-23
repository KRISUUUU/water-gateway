#pragma once

// Minimal stubs for host-testing ESP-IDF component code without the real SDK.
// Only types and macros referenced by pure-logic code need stubs here.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// ESP log level stubs
#define ESP_LOGE(tag, fmt, ...) (void)0
#define ESP_LOGW(tag, fmt, ...) (void)0
#define ESP_LOGI(tag, fmt, ...) (void)0
#define ESP_LOGD(tag, fmt, ...) (void)0
#define ESP_LOGV(tag, fmt, ...) (void)0

// esp_err_t stub
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1

// esp_log_level_t stub
typedef enum {
    ESP_LOG_NONE = 0,
    ESP_LOG_ERROR = 1,
    ESP_LOG_WARN = 2,
    ESP_LOG_INFO = 3,
    ESP_LOG_DEBUG = 4,
    ESP_LOG_VERBOSE = 5,
} esp_log_level_t;
