#pragma once

#include <cstdint>

namespace common {

enum class ErrorCode : uint16_t {
    OK = 0,

    // General (1xx)
    Unknown = 100,
    InvalidArgument = 101,
    NotInitialized = 102,
    AlreadyInitialized = 103,
    NotSupported = 104,
    Timeout = 105,
    BufferFull = 106,
    NotFound = 107,
    AlreadyExists = 108,

    // Validation (2xx)
    ValidationFailed = 200,
    FieldRequired = 201,
    FieldTooLong = 202,
    FieldOutOfRange = 203,
    FormatInvalid = 204,

    // Storage / NVS (3xx)
    StorageReadFailed = 300,
    StorageWriteFailed = 301,
    StorageNotMounted = 302,
    StorageCorrupted = 303,
    NvsOpenFailed = 310,
    NvsReadFailed = 311,
    NvsWriteFailed = 312,
    NvsEraseFailed = 313,

    // Config (4xx)
    ConfigInvalid = 400,
    ConfigVersionMismatch = 401,
    ConfigMigrationFailed = 402,
    ConfigNotLoaded = 403,

    // WiFi (5xx)
    WifiConnectFailed = 500,
    WifiDisconnected = 501,
    WifiApStartFailed = 502,

    // MQTT (6xx)
    MqttConnectFailed = 600,
    MqttPublishFailed = 601,
    MqttDisconnected = 602,
    MqttNotConnected = 603,

    // HTTP (7xx)
    HttpStartFailed = 700,
    HttpHandlerError = 701,
    HttpAuthRequired = 702,
    HttpBadRequest = 703,

    // OTA (8xx)
    OtaBeginFailed = 800,
    OtaWriteFailed = 801,
    OtaValidationFailed = 802,
    OtaCommitFailed = 803,
    OtaAlreadyInProgress = 804,
    OtaImageTooLarge = 805,

    // Radio (9xx)
    RadioInitFailed = 900,
    RadioSpiError = 901,
    RadioFifoOverflow = 902,
    RadioResetFailed = 903,
    RadioNotReady = 904,

    // Auth (10xx)
    AuthFailed = 1000,
    AuthSessionExpired = 1001,
    AuthSessionInvalid = 1002,
    AuthRateLimited = 1003,
};

inline const char* error_code_to_string(ErrorCode code) {
    switch (code) {
    case ErrorCode::OK:
        return "OK";
    case ErrorCode::Unknown:
        return "Unknown";
    case ErrorCode::InvalidArgument:
        return "InvalidArgument";
    case ErrorCode::NotInitialized:
        return "NotInitialized";
    case ErrorCode::AlreadyInitialized:
        return "AlreadyInitialized";
    case ErrorCode::NotSupported:
        return "NotSupported";
    case ErrorCode::Timeout:
        return "Timeout";
    case ErrorCode::BufferFull:
        return "BufferFull";
    case ErrorCode::NotFound:
        return "NotFound";
    case ErrorCode::AlreadyExists:
        return "AlreadyExists";
    case ErrorCode::ValidationFailed:
        return "ValidationFailed";
    case ErrorCode::FieldRequired:
        return "FieldRequired";
    case ErrorCode::FieldTooLong:
        return "FieldTooLong";
    case ErrorCode::FieldOutOfRange:
        return "FieldOutOfRange";
    case ErrorCode::FormatInvalid:
        return "FormatInvalid";
    case ErrorCode::StorageReadFailed:
        return "StorageReadFailed";
    case ErrorCode::StorageWriteFailed:
        return "StorageWriteFailed";
    case ErrorCode::StorageNotMounted:
        return "StorageNotMounted";
    case ErrorCode::StorageCorrupted:
        return "StorageCorrupted";
    case ErrorCode::NvsOpenFailed:
        return "NvsOpenFailed";
    case ErrorCode::NvsReadFailed:
        return "NvsReadFailed";
    case ErrorCode::NvsWriteFailed:
        return "NvsWriteFailed";
    case ErrorCode::NvsEraseFailed:
        return "NvsEraseFailed";
    case ErrorCode::ConfigInvalid:
        return "ConfigInvalid";
    case ErrorCode::ConfigVersionMismatch:
        return "ConfigVersionMismatch";
    case ErrorCode::ConfigMigrationFailed:
        return "ConfigMigrationFailed";
    case ErrorCode::ConfigNotLoaded:
        return "ConfigNotLoaded";
    case ErrorCode::WifiConnectFailed:
        return "WifiConnectFailed";
    case ErrorCode::WifiDisconnected:
        return "WifiDisconnected";
    case ErrorCode::WifiApStartFailed:
        return "WifiApStartFailed";
    case ErrorCode::MqttConnectFailed:
        return "MqttConnectFailed";
    case ErrorCode::MqttPublishFailed:
        return "MqttPublishFailed";
    case ErrorCode::MqttDisconnected:
        return "MqttDisconnected";
    case ErrorCode::MqttNotConnected:
        return "MqttNotConnected";
    case ErrorCode::HttpStartFailed:
        return "HttpStartFailed";
    case ErrorCode::HttpHandlerError:
        return "HttpHandlerError";
    case ErrorCode::HttpAuthRequired:
        return "HttpAuthRequired";
    case ErrorCode::HttpBadRequest:
        return "HttpBadRequest";
    case ErrorCode::OtaBeginFailed:
        return "OtaBeginFailed";
    case ErrorCode::OtaWriteFailed:
        return "OtaWriteFailed";
    case ErrorCode::OtaValidationFailed:
        return "OtaValidationFailed";
    case ErrorCode::OtaCommitFailed:
        return "OtaCommitFailed";
    case ErrorCode::OtaAlreadyInProgress:
        return "OtaAlreadyInProgress";
    case ErrorCode::OtaImageTooLarge:
        return "OtaImageTooLarge";
    case ErrorCode::RadioInitFailed:
        return "RadioInitFailed";
    case ErrorCode::RadioSpiError:
        return "RadioSpiError";
    case ErrorCode::RadioFifoOverflow:
        return "RadioFifoOverflow";
    case ErrorCode::RadioResetFailed:
        return "RadioResetFailed";
    case ErrorCode::RadioNotReady:
        return "RadioNotReady";
    case ErrorCode::AuthFailed:
        return "AuthFailed";
    case ErrorCode::AuthSessionExpired:
        return "AuthSessionExpired";
    case ErrorCode::AuthSessionInvalid:
        return "AuthSessionInvalid";
    case ErrorCode::AuthRateLimited:
        return "AuthRateLimited";
    default:
        return "UnknownError";
    }
}

} // namespace common
