#include "persistent_log_buffer/persistent_log_buffer.hpp"

#ifndef HOST_TEST_BUILD
#include "esp_timer.h"
#endif

namespace persistent_log_buffer {

PersistentLogBuffer& PersistentLogBuffer::instance() {
    static PersistentLogBuffer buffer;
    return buffer;
}

common::Result<void> PersistentLogBuffer::append(LogSeverity severity,
                                                 const char* message) {
    if (!message) {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }

    LogEntry entry{};
    entry.severity = severity;
    entry.message = message;

#ifndef HOST_TEST_BUILD
    entry.timestamp_us = esp_timer_get_time();
#else
    entry.timestamp_us = 0;
#endif

    std::lock_guard<std::mutex> lock(mutex_);
    if (entries_.size() >= kMaxLines) {
        entries_.pop_front();
    }
    entries_.push_back(std::move(entry));
    return common::Result<void>::ok();
}

std::vector<LogEntry> PersistentLogBuffer::lines() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::vector<LogEntry>(entries_.begin(), entries_.end());
}

} // namespace persistent_log_buffer
