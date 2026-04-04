#pragma once

#include <cstdint>

#ifndef HOST_TEST_BUILD
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#else
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#endif

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
    void clear();
    void record_isr_edge(GdoPin pin);
    [[nodiscard]] GdoIrqSnapshot snapshot() const;
    [[nodiscard]] GdoIrqSnapshot take_and_clear();

  private:
#ifndef HOST_TEST_BUILD
    mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
#endif
    volatile uint32_t pending_mask_ = 0U;
    volatile uint32_t gdo0_edges_ = 0U;
    volatile uint32_t gdo2_edges_ = 0U;
};

} // namespace radio_cc1101
