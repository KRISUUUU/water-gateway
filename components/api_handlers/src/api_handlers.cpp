#include "api_handlers/api_handlers.hpp"

namespace api_handlers {

JsonResponse health_response() {
    return JsonResponse{200, "{\"status\":\"ok\"}", "application/json"};
}

JsonResponse not_found_response() {
    return JsonResponse{404, "{\"error\":\"not_found\"}", "application/json"};
}

JsonResponse unauthorized_response() {
    return JsonResponse{401, "{\"error\":\"unauthorized\"}", "application/json"};
}

}  // namespace api_handlers
