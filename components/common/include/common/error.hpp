#pragma once

#include <cstdint>

namespace common {

enum class ErrorCode : std::uint16_t {
    Ok = 0,

    InvalidArgument,
    InvalidState,
    NotInitialized,
    AlreadyInitialized,
    NotFound,
    NotSupported,
    Timeout,
    Busy,
    NoMemory,
    StorageFailure,
    ValidationFailed,
    SerializationFailed,
    DeserializationFailed,
    AuthFailed,
    AccessDenied,
    NetworkFailure,
    MqttFailure,
    HttpFailure,
    OtaFailure,
    RadioFailure,
    RecoveryTriggered,
    InternalError
};

}  // namespace common
