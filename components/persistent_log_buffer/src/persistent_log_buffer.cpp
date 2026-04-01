#include "persistent_log_buffer/persistent_log_buffer.hpp"

#include <cstring>

#ifndef HOST_TEST_BUILD
#include "esp_timer.h"
#endif

namespace persistent_log_buffer {

PersistentLogBuffer& PersistentLogBuffer::instance() {
    static PersistentLogBuffer buffer;
    return buffer;
}

common::Result<void> PersistentLogBuffer::append(LogSeverity severity, const char* message) {
    if (!message) {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t index = count_ < kMaxLines ? (head_ + count_) % kMaxLines : head_;
    if (count_ < kMaxLines) {
        ++count_;
    } else {
        head_ = (head_ + 1) % kMaxLines;
    }

    LogEntry& entry = entries_[index];
    entry.severity = severity;
#ifndef HOST_TEST_BUILD
    entry.timestamp_us = esp_timer_get_time();
#else
    entry.timestamp_us = 0;
#endif
    std::strncpy(entry.message, message, sizeof(entry.message) - 1);
    entry.message[sizeof(entry.message) - 1] = '\0';
    return common::Result<void>::ok();
}

std::size_t PersistentLogBuffer::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
}

bool PersistentLogBuffer::copy_at(std::size_t index, LogEntry& entry) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= count_) {
        return false;
    }
    entry = entries_[(head_ + index) % kMaxLines];
    return true;
}

std::vector<LogEntry> PersistentLogBuffer::lines() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LogEntry> snapshot;
    snapshot.reserve(count_);
    for (std::size_t i = 0; i < count_; ++i) {
        snapshot.push_back(entries_[(head_ + i) % kMaxLines]);
    }
    return snapshot;
}

} // namespace persistent_log_buffer
