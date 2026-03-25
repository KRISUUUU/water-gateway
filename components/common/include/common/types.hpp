#pragma once

#include <cstdint>
#include <cstring>

namespace common {

using TimestampMs = int64_t;

enum class LogSeverity : uint8_t {
    Error = 0,
    Warning = 1,
    Info = 2,
    Debug = 3,
};

enum class SystemMode : uint8_t {
    Provisioning = 0,
    Normal = 1,
};

struct FirmwareVersion {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;

    static constexpr FirmwareVersion current() {
        return {0, 1, 0};
    }
};

struct DeviceIdentity {
    char hostname[32];
    char mac_address[18]; // "AA:BB:CC:DD:EE:FF\0"
    FirmwareVersion firmware;

    static DeviceIdentity make_default() {
        DeviceIdentity id{};
        std::strncpy(id.hostname, "wmbus-gw", sizeof(id.hostname) - 1);
        std::memset(id.mac_address, 0, sizeof(id.mac_address));
        id.firmware = FirmwareVersion::current();
        return id;
    }
};

// Fixed-capacity string buffer for contexts where std::string is undesirable
// (ISR-adjacent code, small embedded buffers in structs).
template <size_t N> struct FixedString {
    char data[N];

    FixedString() {
        data[0] = '\0';
    }

    explicit FixedString(const char* src) {
        if (src) {
            std::strncpy(data, src, N - 1);
            data[N - 1] = '\0';
        } else {
            data[0] = '\0';
        }
    }

    const char* c_str() const {
        return data;
    }
    bool empty() const {
        return data[0] == '\0';
    }
    size_t length() const {
        return std::strlen(data);
    }

    void set(const char* src) {
        if (src) {
            std::strncpy(data, src, N - 1);
            data[N - 1] = '\0';
        } else {
            data[0] = '\0';
        }
    }

    void clear() {
        data[0] = '\0';
    }

    bool operator==(const FixedString& other) const {
        return std::strcmp(data, other.data) == 0;
    }

    bool operator!=(const FixedString& other) const {
        return !(*this == other);
    }
};

} // namespace common
