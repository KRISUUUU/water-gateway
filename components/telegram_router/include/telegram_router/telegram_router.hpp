#pragma once

#include "wmbus_minimal_pipeline/wmbus_frame.hpp"
#include <cstdint>

namespace telegram_router {

enum class RouteDecision : uint8_t {
    PublishRaw = 0,      // Publish to MQTT rf/raw topic
    SuppressDuplicate,   // Frame seen recently, suppress
    PublishEvent,        // Publish as event (e.g., CRC failure)
};

struct RouteResult {
    RouteDecision decision;
    bool publish_raw;
    bool publish_event;
    const char* event_message;

    static RouteResult publish() {
        return {RouteDecision::PublishRaw, true, false, nullptr};
    }

    static RouteResult suppress() {
        return {RouteDecision::SuppressDuplicate, false, false, nullptr};
    }

    static RouteResult event(const char* message) {
        return {RouteDecision::PublishEvent, true, true, message};
    }
};

struct RouterCounters {
    uint32_t frames_routed = 0;
    uint32_t frames_published = 0;
    uint32_t frames_duplicate = 0;
    uint32_t frames_crc_fail_published = 0;
};

class TelegramRouter {
public:
    static TelegramRouter& instance();

    // Route a frame: check dedup, check CRC, decide what to do.
    RouteResult route(const wmbus_minimal_pipeline::WmbusFrame& frame);

    const RouterCounters& counters() const { return counters_; }

    void set_dedup_window_ms(int64_t window_ms);

private:
    TelegramRouter() = default;

    RouterCounters counters_{};
};

} // namespace telegram_router
