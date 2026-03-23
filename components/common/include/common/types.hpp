#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace common {

using TimestampMs = std::uint64_t;
using Milliseconds = std::uint32_t;
using ByteBuffer = std::vector<std::uint8_t>;

enum class LogSeverity : std::uint8_t {
    Debug = 0,
    Info,
    Warn,
    Error
};

enum class SystemMode : std::uint8_t {
    Unconfigured = 0,
    Provisioning,
    Normal,
    Maintenance
};

struct BuildInfo {
    std::string version;
    std::string git_revision;
    std::string build_time_utc;
};

struct DeviceIdentity {
    std::string device_name;
    std::string hostname;
    std::string hardware_model;
    std::string firmware_name;
};

}  // namespace common
