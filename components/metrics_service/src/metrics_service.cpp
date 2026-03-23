#include "metrics_service/metrics_service.hpp"

namespace metrics_service {

MetricsService& MetricsService::instance() {
    static MetricsService service;
    return service;
}

void MetricsService::initialize() {
}

RuntimeMetrics MetricsService::snapshot() const {
    return RuntimeMetrics{};
}

}  // namespace metrics_service
