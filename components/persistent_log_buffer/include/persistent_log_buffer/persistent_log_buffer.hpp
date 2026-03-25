#pragma once

#include "common/result.hpp"
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace persistent_log_buffer {

enum class LogSeverity : std::uint8_t {
    Debug = 0,
    Info,
    Warning,
    Error,
};

struct LogEntry {
    /// Monotonic time from esp_timer_get_time() (microseconds).
    std::int64_t timestamp_us{0};
    LogSeverity severity{LogSeverity::Info};
    std::string message{};
};

class PersistentLogBuffer {
  public:
    static constexpr std::size_t kMaxLines = 200;

    static PersistentLogBuffer& instance();

    common::Result<void> append(LogSeverity severity, const char* message);

    [[nodiscard]] std::vector<LogEntry> lines() const;

  private:
    PersistentLogBuffer() = default;

    mutable std::mutex mutex_{};
    std::deque<LogEntry> entries_{};
};

} // namespace persistent_log_buffer
