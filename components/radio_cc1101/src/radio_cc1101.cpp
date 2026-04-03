#include "radio_cc1101/radio_cc1101.hpp"

#include "cc1101_device.hpp"
#include "cc1101_hal.hpp"
#include "cc1101_irq_hw.hpp"
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

#ifdef HOST_TEST_BUILD
constexpr uint32_t kRawBurstMaxDurationTicks = 20U;
#else
constexpr TickType_t kRawBurstMaxDurationTicks = pdMS_TO_TICKS(20);
#endif
constexpr uint32_t kBurstEndEmptyPolls = 2U;

#ifndef HOST_TEST_BUILD
void prefix_to_hex(const uint8_t* data, uint8_t length, char* out, size_t out_size) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    if (!out || out_size == 0U) {
        return;
    }
    size_t pos = 0;
    for (uint8_t i = 0; i < length && (pos + 2U) < out_size; ++i) {
        out[pos++] = kHex[(data[i] >> 4U) & 0x0F];
        out[pos++] = kHex[data[i] & 0x0F];
    }
    out[pos] = '\0';
}

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

RawBurstTimeoutAction raw_burst_timeout_action(uint32_t elapsed_ticks, uint16_t received,
                                               uint32_t max_duration_ticks) {
    if (elapsed_ticks < max_duration_ticks) {
        return RawBurstTimeoutAction::Continue;
    }
    if (received == 0U) {
        return RawBurstTimeoutAction::ReturnNotFound;
    }
    return RawBurstTimeoutAction::EndBurst;
}

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

    ESP_LOGI(TAG,
             "RF_HOTFIX_V2 active timeout_ticks=%lu timeout_ms=%lu max_data=%u pktctrl0=0x02",
             static_cast<unsigned long>(kRawBurstMaxDurationTicks),
             static_cast<unsigned long>(pdTICKS_TO_MS(kRawBurstMaxDurationTicks)),
             static_cast<unsigned int>(RawRadioFrame::MAX_DATA_SIZE));
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

common::Result<RawRadioFrame> RadioCc1101::read_frame() {
    if (!initialized_) {
        return common::Result<RawRadioFrame>::error(common::ErrorCode::NotInitialized);
    }

    counters_.rx_read_calls++;

#ifndef HOST_TEST_BUILD
    auto marc_result = device::read_marcstate(spi_handle_, counters_);
    if (marc_result.is_error()) {
        state_ = RadioState::Error;
        return common::Result<RawRadioFrame>::error(marc_result.error());
    }
    if (marc_result.value() == registers::MARCSTATE_RXFIFO_OVERFLOW) {
        counters_.fifo_overflows++;
        ESP_LOGW(TAG, "RX FIFO overflow detected");
        auto flush_result = flush_rx_fifo();
        if (flush_result.is_error()) {
            return common::Result<RawRadioFrame>::error(flush_result.error());
        }
        if (!safe_strobe(spi_handle_, counters_, registers::SRX)) {
            state_ = RadioState::Error;
            return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioSpiError);
        }
        state_ = RadioState::Error;
        return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioFifoOverflow);
    }

    uint8_t rx_bytes = 0;
    if (!safe_read_register(spi_handle_, counters_, registers::RXBYTES | registers::READ_BURST,
                            rx_bytes)) {
        state_ = RadioState::Error;
        return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioSpiError);
    }
    const uint8_t num_bytes = rx_bytes & 0x7FU;
    const bool overflow = (rx_bytes & 0x80U) != 0U;

    if (overflow) {
        counters_.fifo_overflows++;
        auto flush_result = flush_rx_fifo();
        if (flush_result.is_error()) {
            return common::Result<RawRadioFrame>::error(flush_result.error());
        }
        if (!safe_strobe(spi_handle_, counters_, registers::SRX)) {
            state_ = RadioState::Error;
            return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioSpiError);
        }
        state_ = RadioState::Error;
        return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioFifoOverflow);
    }

    if (num_bytes == 0U) {
        counters_.rx_not_found++;
        return common::Result<RawRadioFrame>::error(common::ErrorCode::NotFound);
    }

    RawRadioFrame frame{};
    frame.payload_offset = 0;
    frame.radio_crc_available = false;

    uint16_t received = 0;
    const TickType_t start_tick = xTaskGetTickCount();
    uint32_t consecutive_empty_polls = 0;

    while (true) {
        const TickType_t now_tick = xTaskGetTickCount();
        const uint32_t elapsed_ms =
            static_cast<uint32_t>(pdTICKS_TO_MS(static_cast<TickType_t>(now_tick - start_tick)));
        const auto timeout_action = raw_burst_timeout_action(
            static_cast<uint32_t>(now_tick - start_tick), received, kRawBurstMaxDurationTicks);
        if (timeout_action == RawBurstTimeoutAction::ReturnNotFound) {
            counters_.rx_not_found++;
            return common::Result<RawRadioFrame>::error(common::ErrorCode::NotFound);
        }
        if (timeout_action == RawBurstTimeoutAction::EndBurst) {
            frame.capture_elapsed_ms =
                static_cast<uint16_t>(elapsed_ms <= 0xFFFFU ? elapsed_ms : 0xFFFFU);
            frame.burst_end_reason = RadioBurstEndReason::MaxDuration;
            char prefix_hex[17];
            prefix_to_hex(frame.data, static_cast<uint8_t>(received < 8U ? received : 8U),
                          prefix_hex, sizeof(prefix_hex));
            ESP_LOGI(TAG,
                     "RF_HOTFIX_V2 timeout_close len=%u elapsed_ms=%lu first=0x%02X prefix=%s",
                     static_cast<unsigned int>(received), static_cast<unsigned long>(elapsed_ms),
                     static_cast<unsigned int>(received > 0U ? frame.data[0] : 0U), prefix_hex);
            break;
        }

        uint8_t rxb = 0;
        if (!safe_read_register(spi_handle_, counters_, registers::RXBYTES | registers::READ_BURST,
                                rxb)) {
            state_ = RadioState::Error;
            return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioSpiError);
        }
        const uint8_t avail = rxb & 0x7FU;
        if ((rxb & 0x80U) != 0U) {
            counters_.fifo_overflows++;
            auto flush_result = flush_rx_fifo();
            if (flush_result.is_error()) {
                return common::Result<RawRadioFrame>::error(flush_result.error());
            }
            if (!safe_strobe(spi_handle_, counters_, registers::SRX)) {
                state_ = RadioState::Error;
                return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioSpiError);
            }
            state_ = RadioState::Error;
            return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioFifoOverflow);
        }

        if (avail == 0U) {
            if (received > 0U) {
                consecutive_empty_polls++;
                if (consecutive_empty_polls >= kBurstEndEmptyPolls) {
                    frame.burst_end_reason = RadioBurstEndReason::EmptyPolls;
                    break;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        consecutive_empty_polls = 0;
        uint16_t take = avail;
        if (take > registers::FIFO_SIZE) {
            take = registers::FIFO_SIZE;
        }
        const uint16_t remaining_capacity =
            static_cast<uint16_t>(RawRadioFrame::MAX_DATA_SIZE - received);
        if (remaining_capacity == 0U) {
            counters_.frames_dropped_too_long++;
            const uint16_t elapsed_ms_u16 =
                static_cast<uint16_t>(elapsed_ms <= 0xFFFFU ? elapsed_ms : 0xFFFFU);
            frame.capture_elapsed_ms = elapsed_ms_u16;
            device::record_drop(last_drop_, last_drop_mutex_, RadioDropReason::OversizedBurst,
                                frame.data, received, true);
            {
                std::lock_guard<std::mutex> lock(last_drop_mutex_);
                last_drop_.elapsed_ms = elapsed_ms_u16;
            }
            char prefix_hex[17];
            prefix_to_hex(frame.data, last_drop().prefix_length, prefix_hex, sizeof(prefix_hex));
            ESP_LOGW(
                TAG,
                "RF_HOTFIX_V2 oversized_drop len=%u elapsed_ms=%lu first=0x%02X prefix=%s classification=quality_issue",
                static_cast<unsigned int>(received), static_cast<unsigned long>(elapsed_ms),
                static_cast<unsigned int>(received > 0U ? frame.data[0] : 0U), prefix_hex);
            auto flush_result = flush_rx_fifo();
            if (flush_result.is_error()) {
                return common::Result<RawRadioFrame>::error(flush_result.error());
            }
            if (!safe_strobe(spi_handle_, counters_, registers::SRX)) {
                state_ = RadioState::Error;
                return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioSpiError);
            }
            return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioQualityDrop);
        }
        if (take > remaining_capacity) {
            take = remaining_capacity;
        }
        uint8_t chunk[registers::FIFO_SIZE]{};
        if (!safe_read_burst(spi_handle_, counters_, registers::FIFO_RX | registers::READ_BURST,
                             chunk, take)) {
            state_ = RadioState::Error;
            return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioSpiError);
        }
        for (uint16_t i = 0; i < take; ++i) {
            frame.data[received + i] = chunk[i];
        }
        received = static_cast<uint16_t>(received + take);
    }

    if (received == 0U) {
        counters_.rx_not_found++;
        return common::Result<RawRadioFrame>::error(common::ErrorCode::NotFound);
    }

    frame.length = received;
    frame.payload_length = received;
    if (frame.capture_elapsed_ms == 0U) {
        const uint32_t elapsed_ms = static_cast<uint32_t>(
            pdTICKS_TO_MS(static_cast<TickType_t>(xTaskGetTickCount() - start_tick)));
        frame.capture_elapsed_ms = static_cast<uint16_t>(elapsed_ms <= 0xFFFFU ? elapsed_ms : 0xFFFFU);
    }
    frame.first_data_byte = frame.data[0];
    if (frame.burst_end_reason == RadioBurstEndReason::None) {
        frame.burst_end_reason = RadioBurstEndReason::EmptyPolls;
    }

    uint8_t raw_rssi = 0;
    uint8_t raw_lqi = 0;
    if (!safe_read_register(spi_handle_, counters_, registers::RSSI_REG | registers::READ_BURST,
                            raw_rssi) ||
        !safe_read_register(spi_handle_, counters_, registers::LQI_REG | registers::READ_BURST,
                            raw_lqi)) {
        state_ = RadioState::Error;
        return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioSpiError);
    }

    frame.rssi_dbm = device::convert_rssi(raw_rssi);
    frame.lqi = raw_lqi & 0x7FU;
    frame.crc_ok = false;

    counters_.frames_received++;
    return common::Result<RawRadioFrame>::ok(frame);
#else
    RawRadioFrame frame{};
    frame.data[0] = 0x44;
    frame.data[1] = 0x93;
    frame.length = 2;
    frame.payload_offset = 0;
    frame.payload_length = 2;
    frame.first_data_byte = 0x44;
    frame.burst_end_reason = RadioBurstEndReason::EmptyPolls;
    frame.rssi_dbm = -65;
    frame.lqi = 45;
    frame.crc_ok = false;
    frame.radio_crc_available = false;
    counters_.frames_received++;
    return common::Result<RawRadioFrame>::ok(frame);
#endif
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

RadioDropInfo RadioCc1101::last_drop() const {
    std::lock_guard<std::mutex> lock(last_drop_mutex_);
    return last_drop_;
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

} // namespace radio_cc1101
