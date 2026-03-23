#include "telegram_router/telegram_router.hpp"

namespace telegram_router {

TelegramRouter::TelegramRouter(common::Milliseconds dedup_window_ms)
    : dedup_(dedup_window_ms) {
}

RouteDecision TelegramRouter::route(const wmbus_minimal_pipeline::WmbusFrame& frame) {
    RouteDecision decision{};

    const auto& key = frame.raw_hex;
    const auto now = frame.metadata.timestamp_ms;

    if (dedup_.seen_recently(key, now)) {
        decision.is_duplicate = true;
        decision.should_publish_raw = false;
        return decision;
    }

    dedup_.remember(key, now);
    decision.should_publish_raw = true;
    decision.should_publish_event = !frame.metadata.crc_ok;

    return decision;
}

}  // namespace telegram_router
