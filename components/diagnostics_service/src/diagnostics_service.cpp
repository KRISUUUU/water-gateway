#include "diagnostics_service/diagnostics_service.hpp"

namespace diagnostics_service {

DiagnosticsService& DiagnosticsService::instance() {
    static DiagnosticsService service;
    return service;
}

void DiagnosticsService::initialize() {
}

DiagnosticsSnapshot DiagnosticsService::snapshot() const {
    return DiagnosticsSnapshot{
        "{\"diagnostics\":\"placeholder\"}"
    };
}

}  // namespace diagnostics_service
