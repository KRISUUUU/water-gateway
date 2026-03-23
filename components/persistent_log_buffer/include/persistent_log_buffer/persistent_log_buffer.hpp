#pragma once

#include <deque>
#include <string>
#include <vector>

namespace persistent_log_buffer {

class PersistentLogBuffer {
public:
    static PersistentLogBuffer& instance();

    void append(const std::string& line);
    [[nodiscard]] std::vector<std::string> lines() const;

private:
    PersistentLogBuffer() = default;

    std::deque<std::string> lines_;
    static constexpr std::size_t kMaxLines = 200;
};

}  // namespace persistent_log_buffer
