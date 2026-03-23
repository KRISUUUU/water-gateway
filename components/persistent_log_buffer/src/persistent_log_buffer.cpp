#include "persistent_log_buffer/persistent_log_buffer.hpp"

namespace persistent_log_buffer {

PersistentLogBuffer& PersistentLogBuffer::instance() {
    static PersistentLogBuffer buffer;
    return buffer;
}

void PersistentLogBuffer::append(const std::string& line) {
    if (lines_.size() >= kMaxLines) {
        lines_.pop_front();
    }
    lines_.push_back(line);
}

std::vector<std::string> PersistentLogBuffer::lines() const {
    return std::vector<std::string>(lines_.begin(), lines_.end());
}

}  // namespace persistent_log_buffer
