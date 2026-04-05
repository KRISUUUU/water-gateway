#include "radio_cc1101/radio_cc1101.hpp"

#include "cc1101_device.hpp"
#include "cc1101_hal.hpp"
#include "cc1101_irq_hw.hpp"
#include "radio_cc1101/cc1101_profile_prios_r3.hpp"
#include "radio_cc1101/cc1101_profile_tmode.hpp"
#include "radio_cc1101/cc1101_registers.hpp"

#ifndef HOST_TEST_BUILD
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

namespace {
constexpr const char* TAG = "radio_cc1101";
}
#endif

namespace radio_cc1101 {

namespace {

#ifndef HOST_TEST_BUILD
const char* radio_state_str(RadioState state) {
    switch (state) {
    case RadioState::Uninitialized:
        return "Uninitialized";
    case RadioState::Idle:
        return "Idle";
    case RadioState::Receiving:
        return "Receiving";
    case RadioState::Error:
        return "Error";
    }
    return "Unknown";
}
#endif

#ifndef HOST_TEST_BUILD

bool safe_strobe(void* spi_handle, RadioCounters& counters, uint8_t strobe_addr,
                 uint8_t* chip_status = nullptr) {
    if (!hal::spi_strobe(spi_handle, strobe_addr, chip_status)) {
        counters.spi_errors++;
        return false;
    }
    return true;
}

bool safe_read_register(void* spi_handle, RadioCounters& counters, uint8_t addr, uint8_t& value) {
    if (!hal::spi_read_register(spi_handle, addr, value)) {
        counters.spi_errors++;
        return false;
    }
    return true;
}

bool safe_write_register(void* spi_handle, RadioCounters& counters, uint8_t addr, uint8_t value) {
    if (!hal::spi_write_register(spi_handle, addr, value)) {
        counters.spi_errors++;
        return false;
    }
    return true;
}

bool safe_read_burst(void* spi_handle, RadioCounters& counters, uint8_t addr, uint8_t* buffer,
                     size_t length) {
    if (!hal::spi_read_burst(spi_handle, addr, buffer, length)) {
        counters.spi_errors++;
        return false;
    }
    return true;
}
#endif

} // namespace


RadioCc1101& RadioCc1101::instance() {
    static RadioCc1101 radio;
    return radio;
}

common::Result<void> RadioCc1101::initialize(const SpiPins& pins) {
    return initialize(pins, SpiBusConfig{});
}

common::Result<void> RadioCc1101::initialize(const SpiPins& pins, const SpiBusConfig& bus_config) {
    if (initialized_) {
        return common::Result<void>::error(common::ErrorCode::AlreadyInitialized);
    }

    pins_ = pins;
    bus_config_ = bus_config;

    auto spi_init = hal::initialize_spi_device(spi_handle_, pins, bus_config);
    if (spi_init.is_error()) {
        return spi_init;
    }

    const auto cleanup_spi = [&]() {
        disable_gdo_interrupts();
        hal::deinitialize_spi_device(spi_handle_, bus_config.host_id);
    };

    auto gdo_inputs = irq::configure_gdo_inputs(pins);
    if (gdo_inputs.is_error()) {
        cleanup_spi();
        return gdo_inputs;
    }

    auto reset_result = reset();
    if (reset_result.is_error()) {
        cleanup_spi();
        return reset_result;
    }

    if (!verify_chip_id()) {
#ifndef HOST_TEST_BUILD
        ESP_LOGE(TAG, "CC1101 chip ID verification failed");
#endif
        cleanup_spi();
        return common::Result<void>::error(common::ErrorCode::RadioInitFailed);
    }

#ifndef HOST_TEST_BUILD
    auto profile_result =
        device::apply_register_profile(spi_handle_, counters_, kTmodeConfig, kTmodeConfigSize);
    if (profile_result.is_error()) {
        cleanup_spi();
        return profile_result;
    }

    uint8_t sync1 = 0;
    uint8_t sync0 = 0;
    if (!safe_read_register(spi_handle_, counters_, registers::SYNC1, sync1) ||
        !safe_read_register(spi_handle_, counters_, registers::SYNC0, sync0)) {
        cleanup_spi();
        return common::Result<void>::error(common::ErrorCode::RadioSpiError);
    }


    ESP_LOGI(TAG, "T-mode register config applied (%zu registers)", kTmodeConfigSize);
    ESP_LOGI(TAG, "CC1101 sync word: 0x%02X%02X", sync1, sync0);
    ESP_LOGI(TAG, "CC1101 initialized for T-mode 868.95 MHz");
#endif

    state_ = RadioState::Idle;
    initialized_ = true;
    return common::Result<void>::ok();
}

common::Result<void> RadioCc1101::claim_owner(void* owner_token) {
    if (!owner_claim_.claim(owner_token)) {
        return common::Result<void>::error(common::ErrorCode::AlreadyExists);
    }
    return common::Result<void>::ok();
}

void RadioCc1101::release_owner(void* owner_token) {
    if (owner_claim_.owned_by(owner_token)) {
        disable_gdo_interrupts();
        owner_claim_.release(owner_token);
    }
}

bool RadioCc1101::is_owned_by(void* owner_token) const {
    return owner_claim_.owned_by(owner_token);
}

common::Result<void> RadioCc1101::reset() {
#ifndef HOST_TEST_BUILD
    if (!safe_strobe(spi_handle_, counters_, registers::SRES)) {
        state_ = RadioState::Error;
        return common::Result<void>::error(common::ErrorCode::RadioSpiError);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    counters_.radio_resets++;
    ESP_LOGD(TAG, "CC1101 reset complete");
#endif
    state_ = RadioState::Idle;
    return common::Result<void>::ok();
}

common::Result<void> RadioCc1101::start_rx() {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }

#ifndef HOST_TEST_BUILD
    if (!safe_strobe(spi_handle_, counters_, registers::SRX)) {
        state_ = RadioState::Error;
        return common::Result<void>::error(common::ErrorCode::RadioSpiError);
    }

    for (int i = 0; i < 50; ++i) {
        vTaskDelay(pdMS_TO_TICKS(2));
        auto marc_check = device::read_marcstate(spi_handle_, counters_);
        if (marc_check.is_ok() && marc_check.value() == registers::MARCSTATE_RX) {
            break;
        }
    }

    auto marc_result = device::read_marcstate(spi_handle_, counters_);
    if (marc_result.is_error()) {
        state_ = RadioState::Error;
        return common::Result<void>::error(marc_result.error());
    }
    if (marc_result.value() != registers::MARCSTATE_RX) {
        ESP_LOGW(TAG, "Expected MARCSTATE_RX, got 0x%02X", marc_result.value());
    }
#endif

    state_ = RadioState::Receiving;
    return common::Result<void>::ok();
}

common::Result<void> RadioCc1101::go_idle() {
#ifndef HOST_TEST_BUILD
    if (!safe_strobe(spi_handle_, counters_, registers::SIDLE)) {
        state_ = RadioState::Error;
        return common::Result<void>::error(common::ErrorCode::RadioSpiError);
    }
#endif
    state_ = RadioState::Idle;
    return common::Result<void>::ok();
}


common::Result<void> RadioCc1101::flush_rx_fifo() {
#ifndef HOST_TEST_BUILD
    if (!safe_strobe(spi_handle_, counters_, registers::SIDLE) ||
        !safe_strobe(spi_handle_, counters_, registers::SFRX)) {
        state_ = RadioState::Error;
        return common::Result<void>::error(common::ErrorCode::RadioSpiError);
    }
    ESP_LOGD(TAG, "RX FIFO flushed");
#endif
    return common::Result<void>::ok();
}


common::Result<void> RadioCc1101::recover() {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }

#ifndef HOST_TEST_BUILD
    ESP_LOGW(TAG, "Radio recovery initiated");
#endif

    auto reset_result = reset();
    if (reset_result.is_error()) {
        state_ = RadioState::Error;
        return reset_result;
    }

#ifndef HOST_TEST_BUILD
    auto profile_result =
        device::apply_register_profile(spi_handle_, counters_, kTmodeConfig, kTmodeConfigSize);
    if (profile_result.is_error()) {
        state_ = RadioState::Error;
        return profile_result;
    }
#endif

    auto rx_result = start_rx();
    if (rx_result.is_error()) {
        state_ = RadioState::Error;
        return rx_result;
    }

    counters_.radio_recoveries++;
    state_ = RadioState::Receiving;
    return common::Result<void>::ok();
}

bool RadioCc1101::verify_chip_id() {
#ifndef HOST_TEST_BUILD
    uint8_t partnum = 0;
    uint8_t version = 0;
    if (!device::verify_chip_id(spi_handle_, counters_, partnum, version)) {
        return false;
    }
    ESP_LOGI(TAG, "CC1101 PARTNUM=0x%02X VERSION=0x%02X", partnum, version);
    return partnum == registers::CC1101_PARTNUM && version == registers::CC1101_VERSION_EXPECTED;
#else
    return true;
#endif
}

common::Result<void> RadioCc1101::enable_gdo_interrupts(void* owner_token, void* owner_task_handle) {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    if (!owner_claim_.owned_by(owner_token) || !owner_task_handle) {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }
    auto result = irq::enable_gdo_interrupts(pins_, irq_tracker_, owner_task_handle);
    if (result.is_ok()) {
        irq_plumbing_enabled_ = true;
    }
    return result;
}

void RadioCc1101::disable_gdo_interrupts() {
    if (!irq_plumbing_enabled_) {
        return;
    }
    irq::disable_gdo_interrupts(pins_);
    irq_plumbing_enabled_ = false;
    irq_tracker_.clear();
}

GdoIrqSnapshot RadioCc1101::take_gdo_irq_snapshot() {
    return irq_tracker_.take_and_clear();
}

RadioOwnerEventSet RadioCc1101::take_owner_events() {
    return make_owner_events_from_irq(take_gdo_irq_snapshot());
}

common::Result<RxFifoStatus> RadioCc1101::owner_read_rx_status(void* owner_token) {
    if (!initialized_) {
        return common::Result<RxFifoStatus>::error(common::ErrorCode::NotInitialized);
    }
    if (!owner_claim_.owned_by(owner_token)) {
        return common::Result<RxFifoStatus>::error(common::ErrorCode::InvalidArgument);
    }

#ifndef HOST_TEST_BUILD
    auto marc_result = device::read_marcstate(spi_handle_, counters_);
    if (marc_result.is_error()) {
        state_ = RadioState::Error;
        return common::Result<RxFifoStatus>::error(marc_result.error());
    }

    uint8_t rx_bytes = 0;
    if (!safe_read_register(spi_handle_, counters_, registers::RXBYTES | registers::READ_BURST,
                            rx_bytes)) {
        state_ = RadioState::Error;
        return common::Result<RxFifoStatus>::error(common::ErrorCode::RadioSpiError);
    }

    const bool overflow = (rx_bytes & 0x80U) != 0U ||
                          marc_result.value() == registers::MARCSTATE_RXFIFO_OVERFLOW;
    if (overflow) {
        counters_.fifo_overflows++;
        state_ = RadioState::Error;
    }

    RxFifoStatus status{};
    status.fifo_bytes = static_cast<uint8_t>(rx_bytes & 0x7FU);
    status.fifo_overflow = overflow;
    status.marcstate = marc_result.value();
    status.receiving = marc_result.value() == registers::MARCSTATE_RX;
    return common::Result<RxFifoStatus>::ok(status);
#else
    (void)owner_token;
    return common::Result<RxFifoStatus>::ok(
        RxFifoStatus{2U, false, registers::MARCSTATE_RX, true});
#endif
}

common::Result<uint16_t> RadioCc1101::owner_read_fifo_bytes(void* owner_token, uint8_t* buffer,
                                                            uint16_t capacity) {
    if (!initialized_) {
        return common::Result<uint16_t>::error(common::ErrorCode::NotInitialized);
    }
    if (!owner_claim_.owned_by(owner_token) || !buffer || capacity == 0U) {
        return common::Result<uint16_t>::error(common::ErrorCode::InvalidArgument);
    }

#ifndef HOST_TEST_BUILD
    uint16_t take = capacity;
    if (take > registers::FIFO_SIZE) {
        take = registers::FIFO_SIZE;
    }
    if (!safe_read_burst(spi_handle_, counters_, registers::FIFO_RX | registers::READ_BURST,
                         buffer, take)) {
        state_ = RadioState::Error;
        return common::Result<uint16_t>::error(common::ErrorCode::RadioSpiError);
    }
    return common::Result<uint16_t>::ok(take);
#else
    buffer[0] = 0x44;
    if (capacity >= 2U) {
        buffer[1] = 0x93;
        return common::Result<uint16_t>::ok(2U);
    }
    return common::Result<uint16_t>::ok(1U);
#endif
}

common::Result<RxSignalQuality> RadioCc1101::owner_read_signal_quality(void* owner_token) {
    if (!initialized_) {
        return common::Result<RxSignalQuality>::error(common::ErrorCode::NotInitialized);
    }
    if (!owner_claim_.owned_by(owner_token)) {
        return common::Result<RxSignalQuality>::error(common::ErrorCode::InvalidArgument);
    }

#ifndef HOST_TEST_BUILD
    uint8_t raw_rssi = 0;
    uint8_t raw_lqi = 0;
    if (!safe_read_register(spi_handle_, counters_, registers::RSSI_REG | registers::READ_BURST,
                            raw_rssi) ||
        !safe_read_register(spi_handle_, counters_, registers::LQI_REG | registers::READ_BURST,
                            raw_lqi)) {
        state_ = RadioState::Error;
        return common::Result<RxSignalQuality>::error(common::ErrorCode::RadioSpiError);
    }

    RxSignalQuality quality{};
    quality.rssi_dbm = device::convert_rssi(raw_rssi);
    quality.lqi = static_cast<uint8_t>(raw_lqi & 0x7FU);
    quality.crc_ok = false;
    quality.radio_crc_available = false;
    return common::Result<RxSignalQuality>::ok(quality);
#else
    (void)owner_token;
    return common::Result<RxSignalQuality>::ok(RxSignalQuality{-65, 45, false, false});
#endif
}

common::Result<void> RadioCc1101::owner_switch_to_fixed_length_capture(
    void* owner_token, uint8_t remaining_encoded_bytes) {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    if (!owner_claim_.owned_by(owner_token) || remaining_encoded_bytes == 0U) {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }

#ifndef HOST_TEST_BUILD
    // CC1101-specific assumption: after sync and early framing progress, switching from infinite
    // packet mode to fixed-length mode with the remaining encoded tail will bound the remainder of
    // the FIFO capture. Software exact-frame validation remains authoritative, and this behavior
    // still requires hardware validation on real W-MBus traffic.
    if (!safe_write_register(spi_handle_, counters_, registers::PKTLEN, remaining_encoded_bytes) ||
        !safe_write_register(spi_handle_, counters_, registers::PKTCTRL0, 0x00U)) {
        state_ = RadioState::Error;
        return common::Result<void>::error(common::ErrorCode::RadioSpiError);
    }
#else
    (void)owner_token;
    (void)remaining_encoded_bytes;
#endif
    return common::Result<void>::ok();
}

common::Result<void> RadioCc1101::owner_restore_infinite_packet_mode(void* owner_token) {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    if (!owner_claim_.owned_by(owner_token)) {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }

#ifndef HOST_TEST_BUILD
    const auto* pktlen = find_tmode_register_config(registers::PKTLEN);
    const auto* pktctrl0 = find_tmode_register_config(registers::PKTCTRL0);
    if (pktlen == nullptr || pktctrl0 == nullptr ||
        !safe_write_register(spi_handle_, counters_, pktlen->addr, pktlen->value) ||
        !safe_write_register(spi_handle_, counters_, pktctrl0->addr, pktctrl0->value)) {
        state_ = RadioState::Error;
        return common::Result<void>::error(common::ErrorCode::RadioSpiError);
    }
#else
    (void)owner_token;
#endif
    return common::Result<void>::ok();
}

common::Result<void> RadioCc1101::owner_abort_and_restart_rx(void* owner_token) {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    if (!owner_claim_.owned_by(owner_token)) {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }

    auto restore_result = owner_restore_infinite_packet_mode(owner_token);
    if (restore_result.is_error()) {
        state_ = RadioState::Error;
        return restore_result;
    }

    auto flush_result = flush_rx_fifo();
    if (flush_result.is_error()) {
        state_ = RadioState::Error;
        return flush_result;
    }
    auto rx_result = start_rx();
    if (rx_result.is_error()) {
        state_ = RadioState::Error;
        return rx_result;
    }
    state_ = RadioState::Receiving;
    return common::Result<void>::ok();
}

common::Result<void> RadioCc1101::owner_apply_prios_r3_profile(void* owner_token,
                                                                bool  manchester_enabled) {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    if (!owner_claim_.owned_by(owner_token)) {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }

#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "PRIOS R3 apply: begin variant_%s",
             manchester_enabled ? "manchester_on" : "manchester_off");

    // Transition to idle before touching modem registers.
    if (!safe_strobe(spi_handle_, counters_, registers::SIDLE)) {
        state_ = RadioState::Error;
        return common::Result<void>::error(common::ErrorCode::RadioSpiError);
    }

    size_t count = 0;
    const PriosR3RegisterConfig* profile = prios_r3_profile(manchester_enabled, count);
    auto result = device::apply_register_profile(spi_handle_, counters_, profile, count);
    if (result.is_error()) {
        state_ = RadioState::Error;
        return result;
    }

    ESP_LOGI(TAG, "PRIOS R3 apply: profile_ok variant_%s regs=%zu",
             manchester_enabled ? "manchester_on" : "manchester_off", count);
#else
    (void)manchester_enabled;
#endif

    auto flush_result = flush_rx_fifo();
    if (flush_result.is_error()) {
        state_ = RadioState::Error;
        return flush_result;
    }

    auto rx_result = start_rx();
    if (rx_result.is_error()) {
#ifndef HOST_TEST_BUILD
        ESP_LOGE(TAG, "PRIOS R3 apply: start_rx failed (%d)", static_cast<int>(rx_result.error()));
#endif
        state_ = RadioState::Error;
        return rx_result;
    }

#ifndef HOST_TEST_BUILD
    auto marc_result = device::read_marcstate(spi_handle_, counters_);
    if (marc_result.is_ok()) {
        ESP_LOGI(TAG, "PRIOS R3 apply: rx_started state=%s marc=0x%02X",
                 radio_state_str(state_), marc_result.value());
    } else {
        ESP_LOGW(TAG, "PRIOS R3 apply: rx_started state=%s marc=read_error(%d)",
                 radio_state_str(state_), static_cast<int>(marc_result.error()));
    }
#endif

    return common::Result<void>::ok();
}

common::Result<void> RadioCc1101::owner_apply_prios_r3_discovery_profile(
    void* owner_token, bool manchester_enabled) {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    if (!owner_claim_.owned_by(owner_token)) {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }

#ifndef HOST_TEST_BUILD
    ESP_LOGI(TAG, "PRIOS R3 discovery apply: begin variant_%s",
             manchester_enabled ? "manchester_on" : "manchester_off");

    if (!safe_strobe(spi_handle_, counters_, registers::SIDLE)) {
        state_ = RadioState::Error;
        return common::Result<void>::error(common::ErrorCode::RadioSpiError);
    }

    size_t count = 0;
    const PriosR3RegisterConfig* profile =
        prios_r3_discovery_profile(manchester_enabled, count);
    auto result = device::apply_register_profile(spi_handle_, counters_, profile, count);
    if (result.is_error()) {
        state_ = RadioState::Error;
        return result;
    }

    ESP_LOGI(TAG, "PRIOS R3 discovery apply: profile_ok variant_%s regs=%zu",
             manchester_enabled ? "manchester_on" : "manchester_off", count);
#else
    (void)manchester_enabled;
#endif

    auto flush_result = flush_rx_fifo();
    if (flush_result.is_error()) {
        state_ = RadioState::Error;
        return flush_result;
    }

    auto rx_result = start_rx();
    if (rx_result.is_error()) {
#ifndef HOST_TEST_BUILD
        ESP_LOGE(TAG, "PRIOS R3 discovery apply: start_rx failed (%d)",
                 static_cast<int>(rx_result.error()));
#endif
        state_ = RadioState::Error;
        return rx_result;
    }

    state_ = RadioState::Receiving;
    return common::Result<void>::ok();
}

} // namespace radio_cc1101
