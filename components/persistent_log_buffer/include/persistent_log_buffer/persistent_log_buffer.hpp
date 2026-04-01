#pragma once

#include "common/result.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace persistent_log_buffer {

enum class LogSeverity : std::uint8_t {
    Debug = 0,
    Info,
    Warning,
    Error,
};

struct LogEntry {
    static constexpr std::size_t kMessageCapacity = 256;

    /// Monotonic time from esp_timer_get_time() (microseconds).
    std::int64_t timestamp_us{0};
    LogSeverity severity{LogSeverity::Info};
    char message[kMessageCapacity]{};
};

class PersistentLogBuffer {
  public:
    static constexpr std::size_t kMaxLines = 200;
    static constexpr std::size_t kMaxMessageChars = LogEntry::kMessageCapacity;

    static PersistentLogBuffer& instance();

    common::Result<void> append(LogSeverity severity, const char* message);

    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] bool copy_at(std::size_t index, LogEntry& entry) const;
    [[nodiscard]] std::vector<LogEntry> lines() const;

  private:
    PersistentLogBuffer() = default;

    mutable std::mutex mutex_{};
    std::array<LogEntry, kMaxLines> entries_{};
    std::size_t head_{0};
    std::size_t count_{0};
};

} // namespace persistent_log_buffer
