#include "mqtt_service/mqtt_payloads.hpp"

#include <memory>
#include <string>

#include "cJSON.h"

namespace mqtt_service {

namespace {

using JsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
using JsonStringPtr = std::unique_ptr<char, decltype(&cJSON_free)>;

JsonPtr make_object() {
    return JsonPtr(cJSON_CreateObject(), cJSON_Delete);
}

const char* safe_cstr(const char* s) {
    return s ? s : "";
}

std::string to_unformatted_json(cJSON* root) {
    if (!root) {
        return "{}";
    }
    JsonStringPtr printed(cJSON_PrintUnformatted(root), cJSON_free);
    if (!printed) {
        return "{}";
    }
    return std::string(printed.get());
}

} // namespace

std::string payload_status_online(const char* firmware_version, const char* ip_address,
                                  const char* hostname, uint32_t uptime_s,
                                  const char* health_state) {
    JsonPtr root = make_object();
    if (!root) {
        return "{}";
    }

    cJSON_AddBoolToObject(root.get(), "online", true);
    cJSON_AddStringToObject(root.get(), "firmware_version", safe_cstr(firmware_version));
    cJSON_AddStringToObject(root.get(), "ip_address", safe_cstr(ip_address));
    cJSON_AddStringToObject(root.get(), "hostname", safe_cstr(hostname));
    cJSON_AddNumberToObject(root.get(), "uptime_s", static_cast<double>(uptime_s));
    cJSON_AddStringToObject(root.get(), "health", safe_cstr(health_state));
    return to_unformatted_json(root.get());
}

std::string payload_status_offline() {
    JsonPtr root = make_object();
    if (!root) {
        return "{}";
    }
    cJSON_AddBoolToObject(root.get(), "online", false);
    return to_unformatted_json(root.get());
}

} // namespace mqtt_service
