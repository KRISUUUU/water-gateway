#include "cc1101_irq_hw.hpp"

#include "cc1101_hal.hpp"

#ifndef HOST_TEST_BUILD
#include "driver/gpio.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

using radio_cc1101::GdoIrqTracker;
using radio_cc1101::GdoPin;

struct GdoRegistration {
    int gpio = -1;
    GdoPin pin = GdoPin::Gdo0;
    GdoIrqTracker* tracker = nullptr;
    void* owner_task_handle = nullptr;
};

GdoRegistration g_registrations[2]{};
bool g_isr_service_installed = false;

void IRAM_ATTR gdo_isr_handler(void* arg) {
    auto* reg = static_cast<GdoRegistration*>(arg);
    if (reg && reg->tracker) {
        reg->tracker->record_isr_edge(reg->pin);
        if (reg->owner_task_handle) {
            BaseType_t higher_priority_woken = pdFALSE;
            vTaskNotifyGiveFromISR(static_cast<TaskHandle_t>(reg->owner_task_handle),
                                   &higher_priority_woken);
            if (higher_priority_woken == pdTRUE) {
                portYIELD_FROM_ISR();
            }
        }
    }
}

void remove_registration(GdoRegistration& reg) {
    if (reg.gpio >= 0) {
        gpio_isr_handler_remove(static_cast<gpio_num_t>(reg.gpio));
        gpio_intr_disable(static_cast<gpio_num_t>(reg.gpio));
        gpio_set_intr_type(static_cast<gpio_num_t>(reg.gpio), GPIO_INTR_DISABLE);
    }
    reg = GdoRegistration{};
}

common::Result<void> register_pin_irq(int gpio_num, GdoPin pin, GdoIrqTracker& tracker,
                                      void* owner_task_handle,
                                      GdoRegistration& reg) {
    if (gpio_num < 0) {
        return common::Result<void>::ok();
    }
    reg.gpio = gpio_num;
    reg.pin = pin;
    reg.tracker = &tracker;
    reg.owner_task_handle = owner_task_handle;
    const gpio_num_t gpio = static_cast<gpio_num_t>(gpio_num);
    if (gpio_set_intr_type(gpio, GPIO_INTR_POSEDGE) != ESP_OK ||
        gpio_isr_handler_add(gpio, gdo_isr_handler, &reg) != ESP_OK ||
        gpio_intr_enable(gpio) != ESP_OK) {
        remove_registration(reg);
        return common::Result<void>::error(common::ErrorCode::RadioInitFailed);
    }
    return common::Result<void>::ok();
}

} // namespace
#endif

namespace radio_cc1101::irq {

common::Result<void> configure_gdo_inputs(const SpiPins& pins) {
#ifndef HOST_TEST_BUILD
    auto result = hal::configure_input_pin(pins.gdo0);
    if (result.is_error()) {
        return result;
    }
    return hal::configure_input_pin(pins.gdo2);
#else
    (void)pins;
    return common::Result<void>::ok();
#endif
}

common::Result<void> enable_gdo_interrupts(const SpiPins& pins, GdoIrqTracker& tracker,
                                           void* owner_task_handle) {
#ifndef HOST_TEST_BUILD
    if (!g_isr_service_installed) {
        const esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return common::Result<void>::error(common::ErrorCode::RadioInitFailed);
        }
        g_isr_service_installed = true;
    }
    tracker.clear();
    auto result =
        register_pin_irq(pins.gdo0, GdoPin::Gdo0, tracker, owner_task_handle, g_registrations[0]);
    if (result.is_error()) {
        return result;
    }
    result =
        register_pin_irq(pins.gdo2, GdoPin::Gdo2, tracker, owner_task_handle, g_registrations[1]);
    if (result.is_error()) {
        disable_gdo_interrupts(pins);
        return result;
    }
    return common::Result<void>::ok();
#else
    (void)pins;
    tracker.clear();
    (void)owner_task_handle;
    return common::Result<void>::ok();
#endif
}

void disable_gdo_interrupts(const SpiPins& pins) {
#ifndef HOST_TEST_BUILD
    (void)pins;
    remove_registration(g_registrations[0]);
    remove_registration(g_registrations[1]);
#else
    (void)pins;
#endif
}

} // namespace radio_cc1101::irq
