#include "mdns_service/mdns_service.hpp"

namespace mdns_service {

MdnsService& MdnsService::instance() {
    static MdnsService service;
    return service;
}

common::Result<void> MdnsService::initialize() {
    if (initialized_) {
        return common::Result<void>(common::ErrorCode::AlreadyInitialized);
    }

    initialized_ = true;
    return common::Result<void>();
}

common::Result<void> MdnsService::start(const std::string& hostname) {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    if (hostname.empty()) {
        return common::Result<void>(common::ErrorCode::InvalidArgument);
    }

    return common::Result<void>();
}

}  // namespace mdns_service
