#pragma once

#include "common/result.hpp"
#include "radio_cc1101/cc1101_irq.hpp"
#include "radio_cc1101/radio_cc1101.hpp"

namespace radio_cc1101::irq {

common::Result<void> configure_gdo_inputs(const SpiPins& pins);
common::Result<void> enable_gdo_interrupts(const SpiPins& pins, GdoIrqTracker& tracker,
                                           void* owner_task_handle);
void disable_gdo_interrupts(const SpiPins& pins);

} // namespace radio_cc1101::irq
