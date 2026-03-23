#pragma once

#include <vector>

#include "dedup_service/dedup_service.hpp"
#include "wmbus_minimal_pipeline/wmbus_frame.hpp"

namespace telegram_router {

struct RouteDecision {
    bool is_duplicate{false};
    bool should_publish_raw{true};
    bool should_publish_event{false};
};

class TelegramRouter {
public:
    explicit TelegramRouter(common::Milliseconds dedup_window_ms = 60000);

    RouteDecision route(const wmbus_minimal_pipeline::WmbusFrame& frame);

private:
    dedup_service::DedupService dedup_;
};

}  // namespace telegram_router
