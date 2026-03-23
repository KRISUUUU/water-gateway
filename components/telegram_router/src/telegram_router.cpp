#include "telegram_router/telegram_router.hpp"
#include "dedup_service/dedup_service.hpp"

namespace telegram_router {

TelegramRouter& TelegramRouter::instance() {
    static TelegramRouter router;
    return router;
}

RouteResult TelegramRouter::route(
    const wmbus_minimal_pipeline::WmbusFrame& frame) {

    counters_.frames_routed++;

    auto& dedup = dedup_service::DedupService::instance();
    int64_t now = frame.metadata.timestamp_ms;

    // Dedup check based on raw hex content
    if (dedup.seen_recently(frame.raw_hex, now)) {
        counters_.frames_duplicate++;
        return RouteResult::suppress();
    }

    dedup.remember(frame.raw_hex, now);

    // CRC-failed frames are still published (external decoder may handle them)
    // but we also publish an event for diagnostics
    if (!frame.metadata.crc_ok) {
        counters_.frames_crc_fail_published++;
        counters_.frames_published++;
        return RouteResult::event("Received frame with CRC failure");
    }

    counters_.frames_published++;
    return RouteResult::publish();
}

void TelegramRouter::set_dedup_window_ms(int64_t window_ms) {
    dedup_service::DedupService::instance().set_window_ms(window_ms);
}

} // namespace telegram_router
