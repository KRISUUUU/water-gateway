#pragma once

#include <string>

#include "common/result.hpp"

namespace mdns_service {

class MdnsService {
public:
    static MdnsService& instance();

    common::Result<void> initialize();
    common::Result<void> start(const std::string& hostname);

private:
    MdnsService() = default;
    bool initialized_{false};
};

}  // namespace mdns_service
