#pragma once

#include "radio_cc1101/cc1101_irq.hpp"

#include <cstdint>

namespace radio_cc1101 {

enum class RadioOwnerEvent : uint32_t {
    None = 0U,
    SessionWatchdogTick = 1U << 0U,
    FallbackPoll = 1U << 1U,
    Gdo0Edge = 1U << 2U,
    Gdo2Edge = 1U << 3U,
};

constexpr uint32_t radio_owner_event_bit(RadioOwnerEvent event) {
    return static_cast<uint32_t>(event);
}

struct RadioOwnerEventSet {
    uint32_t mask = 0U;
    GdoIrqSnapshot irq_snapshot{};

    bool has(RadioOwnerEvent event) const {
        return (mask & radio_owner_event_bit(event)) != 0U;
    }

    bool has_any_irq() const {
        return has(RadioOwnerEvent::Gdo0Edge) || has(RadioOwnerEvent::Gdo2Edge);
    }

    // RX work is IRQ-first. FallbackPoll is reserved for bounded liveness checks:
    //   - rare idle fallback when IRQ plumbing is enabled but a notification was missed
    //   - explicit degraded-mode polling when IRQ plumbing is unavailable
    // SessionWatchdogTick remains active only while a session is already in progress.
    bool should_attempt_rx_work(bool session_active) const {
        return has_any_irq() || has(RadioOwnerEvent::FallbackPoll) ||
               (session_active && has(RadioOwnerEvent::SessionWatchdogTick));
    }
};

constexpr RadioOwnerEventSet make_session_watchdog_tick_event() {
    return {radio_owner_event_bit(RadioOwnerEvent::SessionWatchdogTick), {}};
}

constexpr RadioOwnerEventSet make_fallback_poll_event() {
    return {radio_owner_event_bit(RadioOwnerEvent::FallbackPoll), {}};
}

inline RadioOwnerEventSet make_owner_events_from_irq(const GdoIrqSnapshot& irq_snapshot) {
    uint32_t mask = 0U;
    if (irq_snapshot.has_edge(GdoPin::Gdo0)) {
        mask |= radio_owner_event_bit(RadioOwnerEvent::Gdo0Edge);
    }
    if (irq_snapshot.has_edge(GdoPin::Gdo2)) {
        mask |= radio_owner_event_bit(RadioOwnerEvent::Gdo2Edge);
    }
    return {mask, irq_snapshot};
}

inline RadioOwnerEventSet merge_owner_events(const RadioOwnerEventSet& lhs,
                                             const RadioOwnerEventSet& rhs) {
    return {lhs.mask | rhs.mask,
            {lhs.irq_snapshot.pending_mask | rhs.irq_snapshot.pending_mask,
             lhs.irq_snapshot.gdo0_edges + rhs.irq_snapshot.gdo0_edges,
             lhs.irq_snapshot.gdo2_edges + rhs.irq_snapshot.gdo2_edges}};
}

class RadioOwnerClaimState {
  public:
    bool claim(void* owner_token) {
        if (!owner_token) {
            return false;
        }
        if (owner_token_ == nullptr || owner_token_ == owner_token) {
            owner_token_ = owner_token;
            return true;
        }
        return false;
    }

    bool release(void* owner_token) {
        if (owner_token_ != owner_token || owner_token == nullptr) {
            return false;
        }
        owner_token_ = nullptr;
        return true;
    }

    bool owned_by(void* owner_token) const {
        return owner_token_ != nullptr && owner_token_ == owner_token;
    }

    bool is_claimed() const {
        return owner_token_ != nullptr;
    }

  private:
    void* owner_token_ = nullptr;
};

} // namespace radio_cc1101
