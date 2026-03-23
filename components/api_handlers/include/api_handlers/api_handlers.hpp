#pragma once

#include <string>

namespace api_handlers {

struct JsonResponse {
    int status_code{200};
    std::string body{};
    std::string content_type{"application/json"};
};

JsonResponse health_response();
JsonResponse not_found_response();
JsonResponse unauthorized_response();

}  // namespace api_handlers
