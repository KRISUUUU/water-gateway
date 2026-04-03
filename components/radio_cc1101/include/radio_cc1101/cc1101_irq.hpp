#pragma once

#include <atomic>
#include <cstdint>

namespace radio_cc1101 {

enum class GdoPin : uint8_t {
    Gdo0 = 0,
    Gdo2 = 1,
};

struct GdoIrqSnapshot {
    uint32_t pending_mask = 0;
    uint32_t gdo0_edges = 0;
    uint32_t gdo2_edges = 0;

    static constexpr uint32_t bit_for(GdoPin pin) {
        return pin == GdoPin::Gdo0 ? 0x01U : 0x02U;
    }

    bool has_edge(GdoPin pin) const {
        return (pending_mask & bit_for(pin)) != 0U;
    }

    uint32_t edge_count(GdoPin pin) const {
        return pin == GdoPin::Gdo0 ? gdo0_edges : gdo2_edges;
    }
};

class GdoIrqTracker {
  public:
    void clear() {
        pending_mask_.store(0U, std::memory_order_relaxed);
        gdo0_edges_.store(0U, std::memory_order_relaxed);
        gdo2_edges_.store(0U, std::memory_order_relaxed);
    }

    void record_isr_edge(GdoPin pin) {
        pending_mask_.fetch_or(GdoIrqSnapshot::bit_for(pin), std::memory_order_relaxed);
        if (pin == GdoPin::Gdo0) {
            gdo0_edges_.fetch_add(1U, std::memory_order_relaxed);
        } else {
            gdo2_edges_.fetch_add(1U, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] GdoIrqSnapshot snapshot() const {
        return {pending_mask_.load(std::memory_order_relaxed),
                gdo0_edges_.load(std::memory_order_relaxed),
                gdo2_edges_.load(std::memory_order_relaxed)};
    }

    [[nodiscard]] GdoIrqSnapshot take_and_clear() {
        return {pending_mask_.exchange(0U, std::memory_order_relaxed),
                gdo0_edges_.exchange(0U, std::memory_order_relaxed),
                gdo2_edges_.exchange(0U, std::memory_order_relaxed)};
    }

  private:
    std::atomic<uint32_t> pending_mask_{0U};
    std::atomic<uint32_t> gdo0_edges_{0U};
    std::atomic<uint32_t> gdo2_edges_{0U};
};

} // namespace radio_cc1101
