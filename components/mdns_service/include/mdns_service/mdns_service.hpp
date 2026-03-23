#pragma once

#include "common/result.hpp"

namespace mdns_service {

class MdnsService {
public:
    static MdnsService& instance();

    common::Result<void> initialize();
    common::Result<void> start(const char* hostname);
    common::Result<void> stop();

    bool is_started() const { return started_; }

private:
    MdnsService() = default;

    bool initialized_ = false;
    bool started_ = false;
};

} // namespace mdns_service
