#include "support_bundle_service/support_bundle_service.hpp"

namespace support_bundle_service {

SupportBundleService& SupportBundleService::instance() {
    static SupportBundleService service;
    return service;
}

std::string SupportBundleService::generate_bundle_json() const {
    return "{\"support_bundle\":\"placeholder\"}";
}

}  // namespace support_bundle_service
