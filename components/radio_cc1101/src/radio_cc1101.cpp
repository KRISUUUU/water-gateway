#include "radio_cc1101/radio_cc1101.hpp"
#include "radio_cc1101/cc1101_registers.hpp"

#ifndef HOST_TEST_BUILD
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char* TAG = "radio_cc1101";
#endif

namespace radio_cc1101 {

namespace {
#ifdef HOST_TEST_BUILD
constexpr uint32_t kRawBurstMaxDurationTicks = 20;
constexpr uint32_t kBurstEndEmptyPolls = 2;
#else
constexpr TickType_t kRawBurstMaxDurationTicks = pdMS_TO_TICKS(20);
constexpr uint32_t kBurstEndEmptyPolls = 2;

const char* drop_reason_str(RadioDropReason reason) {
    switch (reason) {
    case RadioDropReason::None:
        return "none";
    case RadioDropReason::OversizedBurst:
        return "oversized_burst";
    case RadioDropReason::BurstTimeout:
        return "burst_timeout";
    }
    return "unknown";
}

const char* burst_end_reason_str(RadioBurstEndReason reason) {
    switch (reason) {
    case RadioBurstEndReason::None:
        return "none";
    case RadioBurstEndReason::EmptyPolls:
        return "empty_polls";
    case RadioBurstEndReason::MaxDuration:
        return "max_duration";
    }
    return "unknown";
}

void prefix_to_hex(const uint8_t* data, uint8_t length, char* out, size_t out_size) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    if (!out || out_size == 0) {
        return;
    }
    size_t pos = 0;
    for (uint8_t i = 0; i < length && (pos + 2U) < out_size; ++i) {
        out[pos++] = kHex[(data[i] >> 4U) & 0x0F];
        out[pos++] = kHex[data[i] & 0x0F];
    }
    out[pos] = '\0';
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

#ifndef HOST_TEST_BUILD
    // Configure SPI bus
    spi_bus_config_t bus_cfg{};
    bus_cfg.mosi_io_num = pins.mosi;
    bus_cfg.miso_io_num = pins.miso;
    bus_cfg.sclk_io_num = pins.sck;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = bus_config.max_transfer_size;

    const spi_host_device_t spi_host = static_cast<spi_host_device_t>(bus_config.host_id);
    bool spi_bus_initialized = false;
    bool spi_device_added = false;
    const auto cleanup_spi = [&]() {
        if (spi_device_added && spi_handle_) {
            spi_bus_remove_device(static_cast<spi_device_handle_t>(spi_handle_));
            spi_handle_ = nullptr;
            spi_device_added = false;
        }
        if (spi_bus_initialized) {
            spi_bus_free(spi_host);
            spi_bus_initialized = false;
        }
    };

    esp_err_t err = spi_bus_initialize(spi_host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %d", err);
        return common::Result<void>::error(common::ErrorCode::RadioSpiError);
    }
    spi_bus_initialized = true;

    spi_device_interface_config_t dev_cfg{};
    dev_cfg.clock_speed_hz = static_cast<int>(bus_config.clock_hz);
    dev_cfg.mode = 0;
    dev_cfg.spics_io_num = pins.cs;
    dev_cfg.queue_size = 1;

    err = spi_bus_add_device(spi_host, &dev_cfg,
                             reinterpret_cast<spi_device_handle_t*>(&spi_handle_));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %d", err);
        cleanup_spi();
        return common::Result<void>::error(common::ErrorCode::RadioSpiError);
    }
    spi_device_added = true;

    // Configure GDO pins as inputs if used
    if (pins.gdo0 >= 0) {
        gpio_set_direction(static_cast<gpio_num_t>(pins.gdo0), GPIO_MODE_INPUT);
    }
    if (pins.gdo2 >= 0) {
        gpio_set_direction(static_cast<gpio_num_t>(pins.gdo2), GPIO_MODE_INPUT);
    }

    // Reset and configure
    auto result = reset();
    if (result.is_error()) {
        cleanup_spi();
        return result;
    }

    if (!verify_chip_id()) {
        ESP_LOGE(TAG, "CC1101 chip ID verification failed");
        cleanup_spi();
        return common::Result<void>::error(common::ErrorCode::RadioInitFailed);
    }

    auto config_result = apply_tmode_config();
    if (config_result.is_error()) {
        cleanup_spi();
        return config_result;
    }

    ESP_LOGI(TAG,
             "RF_HOTFIX_V2 active timeout_ticks=%lu timeout_ms=%lu max_data=%u pktctrl0=0x02",
             static_cast<unsigned long>(kRawBurstMaxDurationTicks),
             static_cast<unsigned long>(pdTICKS_TO_MS(kRawBurstMaxDurationTicks)),
             static_cast<unsigned int>(RawRadioFrame::MAX_DATA_SIZE));
    ESP_LOGI(TAG, "CC1101 initialized for T-mode 868.95 MHz");
#endif

    state_ = RadioState::Idle;
    initialized_ = true;
    return common::Result<void>::ok();
}

common::Result<void> RadioCc1101::reset() {
#ifndef HOST_TEST_BUILD
    if (!spi_strobe(registers::SRES)) {
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
    if (!spi_strobe(registers::SRX)) {
        state_ = RadioState::Error;
        return common::Result<void>::error(common::ErrorCode::RadioSpiError);
    }

    // Wait for CC1101 to finish calibration and enter RX.
    for (int i = 0; i < 50; ++i) {
        vTaskDelay(pdMS_TO_TICKS(2));
        auto marc_check = read_marcstate();
        if (marc_check.is_ok() && marc_check.value() == registers::MARCSTATE_RX) {
            break;
        }
    }

    auto marc_result = read_marcstate();
    if (marc_result.is_error()) {
        state_ = RadioState::Error;
        return common::Result<void>::error(marc_result.error());
    }
    uint8_t marc = marc_result.value();
    if (marc != registers::MARCSTATE_RX) {
        ESP_LOGW(TAG, "Expected MARCSTATE_RX, got 0x%02X", marc);
    }
#endif

    state_ = RadioState::Receiving;
    return common::Result<void>::ok();
}

common::Result<void> RadioCc1101::go_idle() {
#ifndef HOST_TEST_BUILD
    if (!spi_strobe(registers::SIDLE)) {
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
    // This function is called from the polling RX task. NotFound is the steady-state "no complete
    // frame yet" result, RadioQualityDrop indicates a burst/framing issue, and FIFO overflow /
    // SPI errors are treated as real radio faults.

    // Check MARCSTATE for FIFO overflow
    auto marc_result = read_marcstate();
    if (marc_result.is_error()) {
        state_ = RadioState::Error;
        return common::Result<RawRadioFrame>::error(marc_result.error());
    }
    uint8_t marc = marc_result.value();
    if (marc == registers::MARCSTATE_RXFIFO_OVERFLOW) {
        counters_.fifo_overflows++;
        ESP_LOGW(TAG, "RX FIFO overflow detected");
        auto flush_result = flush_rx_fifo();
        if (flush_result.is_error()) {
            return common::Result<RawRadioFrame>::error(flush_result.error());
        }
        if (!spi_strobe(registers::SRX)) {
            state_ = RadioState::Error;
            return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioSpiError);
        }
        state_ = RadioState::Error;
        return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioFifoOverflow);
    }

    // Read number of bytes in RX FIFO
    uint8_t rx_bytes = 0;
    if (!spi_read_register(registers::RXBYTES | registers::READ_BURST, rx_bytes)) {
        state_ = RadioState::Error;
        return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioSpiError);
    }
    uint8_t num_bytes = rx_bytes & 0x7F; // Mask off overflow bit
    bool overflow = (rx_bytes & 0x80) != 0;

    if (overflow) {
        counters_.fifo_overflows++;
        auto flush_result = flush_rx_fifo();
        if (flush_result.is_error()) {
            return common::Result<RawRadioFrame>::error(flush_result.error());
        }
        if (!spi_strobe(registers::SRX)) {
            state_ = RadioState::Error;
            return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioSpiError);
        }
        state_ = RadioState::Error;
        return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioFifoOverflow);
    }

    if (num_bytes == 0) {
        counters_.rx_not_found++;
        return common::Result<RawRadioFrame>::error(common::ErrorCode::NotFound);
    }

    RawRadioFrame frame{};
    frame.payload_offset = 0;
    frame.radio_crc_available = false;

    // Infinite-length/raw capture: drain one RX burst after sync. End-of-burst is determined only
    // by the polling path here: either the FIFO stays empty across a couple of consecutive polls,
    // or we hit a bounded max capture window and force-close the burst. We do not use GDO2 here,
    // because the current IOCFG2 setting in kTmodeConfig is configured as a sync indication, not a
    // validated packet-active/end-of-packet signal for this mode.
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
#ifndef HOST_TEST_BUILD
            char prefix_hex[17];
            prefix_to_hex(frame.data, static_cast<uint8_t>(received < 8U ? received : 8U),
                          prefix_hex, sizeof(prefix_hex));
            ESP_LOGI(TAG,
                     "RF_HOTFIX_V2 timeout_close len=%u elapsed_ms=%lu first=0x%02X prefix=%s",
                     static_cast<unsigned int>(received),
                     static_cast<unsigned long>(elapsed_ms),
                     static_cast<unsigned int>(received > 0 ? frame.data[0] : 0U), prefix_hex);
#endif
            break;
        }

        uint8_t rxb = 0;
        if (!spi_read_register(registers::RXBYTES | registers::READ_BURST, rxb)) {
            state_ = RadioState::Error;
            return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioSpiError);
        }
        uint8_t avail = rxb & 0x7F;
        if ((rxb & 0x80) != 0) {
            counters_.fifo_overflows++;
            auto flush_result = flush_rx_fifo();
            if (flush_result.is_error()) {
                return common::Result<RawRadioFrame>::error(flush_result.error());
            }
            if (!spi_strobe(registers::SRX)) {
                state_ = RadioState::Error;
                return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioSpiError);
            }
            state_ = RadioState::Error;
            return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioFifoOverflow);
        }
        if (avail == 0) {
            if (received > 0) {
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
        if (take > 64) {
            take = 64;
        }
        const uint16_t remaining_capacity =
            static_cast<uint16_t>(RawRadioFrame::MAX_DATA_SIZE - received);
        if (remaining_capacity == 0) {
            counters_.frames_dropped_too_long++;
            const uint16_t elapsed_ms_u16 =
                static_cast<uint16_t>(elapsed_ms <= 0xFFFFU ? elapsed_ms : 0xFFFFU);
            frame.capture_elapsed_ms = elapsed_ms_u16;
            record_drop(RadioDropReason::OversizedBurst, frame.data, received, true);
#ifndef HOST_TEST_BUILD
            {
                std::lock_guard<std::mutex> lock(last_drop_mutex_);
                last_drop_.elapsed_ms = elapsed_ms_u16;
            }
            char prefix_hex[17];
            prefix_to_hex(frame.data, last_drop().prefix_length, prefix_hex, sizeof(prefix_hex));
            ESP_LOGW(TAG,
                     "RF_HOTFIX_V2 oversized_drop len=%u elapsed_ms=%lu first=0x%02X prefix=%s classification=quality_issue",
                     static_cast<unsigned int>(received),
                     static_cast<unsigned long>(elapsed_ms),
                     static_cast<unsigned int>(received > 0 ? frame.data[0] : 0U), prefix_hex);
#endif
            auto flush_result = flush_rx_fifo();
            if (flush_result.is_error()) {
                return common::Result<RawRadioFrame>::error(flush_result.error());
            }
            if (!spi_strobe(registers::SRX)) {
                state_ = RadioState::Error;
                return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioSpiError);
            }
            return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioQualityDrop);
        }
        if (take > remaining_capacity) {
            take = remaining_capacity;
        }
        uint8_t chunk[64];
        if (!spi_read_burst(registers::FIFO_RX | registers::READ_BURST, chunk, take)) {
            state_ = RadioState::Error;
            return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioSpiError);
        }
        for (uint16_t i = 0; i < take; ++i) {
            frame.data[received + i] = chunk[i];
        }
        received = static_cast<uint16_t>(received + take);
    }

    if (received == 0) {
        counters_.rx_not_found++;
        return common::Result<RawRadioFrame>::error(common::ErrorCode::NotFound);
    }

    frame.length = received;
    frame.payload_length = received;
    if (frame.capture_elapsed_ms == 0U) {
        const uint32_t elapsed_ms =
            static_cast<uint32_t>(pdTICKS_TO_MS(static_cast<TickType_t>(xTaskGetTickCount() - start_tick)));
        frame.capture_elapsed_ms = static_cast<uint16_t>(elapsed_ms <= 0xFFFFU ? elapsed_ms : 0xFFFFU);
    }
    frame.first_data_byte = frame.data[0];
    if (frame.burst_end_reason == RadioBurstEndReason::None) {
        frame.burst_end_reason = RadioBurstEndReason::EmptyPolls;
    }

    uint8_t raw_rssi = 0;
    uint8_t raw_lqi = 0;
    if (!spi_read_register(registers::RSSI_REG | registers::READ_BURST, raw_rssi) ||
        !spi_read_register(registers::LQI_REG | registers::READ_BURST, raw_lqi)) {
        state_ = RadioState::Error;
        return common::Result<RawRadioFrame>::error(common::ErrorCode::RadioSpiError);
    }

    frame.rssi_dbm = convert_rssi(raw_rssi);
    frame.lqi = raw_lqi & 0x7F;
    frame.crc_ok = false;

    counters_.frames_received++;

    return common::Result<RawRadioFrame>::ok(frame);
#else
    // Host test stub: return a synthetic frame
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
    if (!spi_strobe(registers::SIDLE) || !spi_strobe(registers::SFRX)) {
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
    auto config_result = apply_tmode_config();
    if (config_result.is_error()) {
        state_ = RadioState::Error;
        return config_result;
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
    if (!spi_read_register(registers::PARTNUM | registers::READ_BURST, partnum) ||
        !spi_read_register(registers::VERSION | registers::READ_BURST, version)) {
        return false;
    }

    ESP_LOGI(TAG, "CC1101 PARTNUM=0x%02X VERSION=0x%02X", partnum, version);

    return (partnum == registers::CC1101_PARTNUM && version == registers::CC1101_VERSION_EXPECTED);
#else
    return true;
#endif
}

#ifndef HOST_TEST_BUILD

void RadioCc1101::record_drop(RadioDropReason reason, const uint8_t* data, uint16_t length,
                              bool quality_issue) {
    std::lock_guard<std::mutex> lock(last_drop_mutex_);
    last_drop_.reason = reason;
    last_drop_.captured_length = length;
    last_drop_.elapsed_ms = 0;
    last_drop_.first_data_byte = (data && length > 0) ? data[0] : 0U;
    last_drop_.quality_issue = quality_issue;
    last_drop_.prefix_length = static_cast<uint8_t>(length < sizeof(last_drop_.prefix)
                                                        ? length
                                                        : sizeof(last_drop_.prefix));
    for (uint8_t i = 0; i < last_drop_.prefix_length; ++i) {
        last_drop_.prefix[i] = data[i];
    }
    for (uint8_t i = last_drop_.prefix_length; i < sizeof(last_drop_.prefix); ++i) {
        last_drop_.prefix[i] = 0;
    }
}

bool RadioCc1101::spi_strobe(uint8_t strobe_addr, uint8_t* chip_status) {
    spi_transaction_t txn{};
    txn.length = 8;
    txn.tx_data[0] = strobe_addr;
    txn.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;

    esp_err_t err = spi_device_transmit(static_cast<spi_device_handle_t>(spi_handle_), &txn);
    if (err != ESP_OK) {
        counters_.spi_errors++;
        return false;
    }
    if (chip_status) {
        *chip_status = txn.rx_data[0];
    }
    return true;
}

bool RadioCc1101::spi_read_register(uint8_t addr, uint8_t& value) {
    spi_transaction_t txn{};
    txn.length = 16;
    txn.tx_data[0] = addr | registers::READ_SINGLE;
    txn.tx_data[1] = 0x00;
    txn.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;

    esp_err_t err = spi_device_transmit(static_cast<spi_device_handle_t>(spi_handle_), &txn);
    if (err != ESP_OK) {
        counters_.spi_errors++;
        return false;
    }
    value = txn.rx_data[1];
    return true;
}

bool RadioCc1101::spi_write_register(uint8_t addr, uint8_t value) {
    spi_transaction_t txn{};
    txn.length = 16;
    txn.tx_data[0] = addr;
    txn.tx_data[1] = value;
    txn.flags = SPI_TRANS_USE_TXDATA;

    esp_err_t err = spi_device_transmit(static_cast<spi_device_handle_t>(spi_handle_), &txn);
    if (err != ESP_OK) {
        counters_.spi_errors++;
        return false;
    }
    return true;
}

bool RadioCc1101::spi_read_burst(uint8_t addr, uint8_t* buffer, size_t length) {
    if (!buffer || length == 0)
        return false;

    uint8_t tx_buf[65]{};
    uint8_t rx_buf[65]{};
    tx_buf[0] = addr;

    spi_transaction_t txn{};
    txn.length = (length + 1) * 8;
    txn.tx_buffer = tx_buf;
    txn.rx_buffer = rx_buf;

    esp_err_t err = spi_device_transmit(static_cast<spi_device_handle_t>(spi_handle_), &txn);
    if (err != ESP_OK) {
        counters_.spi_errors++;
        return false;
    }
    std::memcpy(buffer, &rx_buf[1], length);
    return true;
}

common::Result<void> RadioCc1101::apply_tmode_config() {
    for (size_t i = 0; i < kTmodeConfigSize; ++i) {
        if (!spi_write_register(kTmodeConfig[i].addr, kTmodeConfig[i].value)) {
            return common::Result<void>::error(common::ErrorCode::RadioSpiError);
        }
    }

    uint8_t sync1 = 0;
    uint8_t sync0 = 0;
    if (!spi_read_register(registers::SYNC1, sync1) || !spi_read_register(registers::SYNC0, sync0)) {
        return common::Result<void>::error(common::ErrorCode::RadioSpiError);
    }

    ESP_LOGI(TAG, "T-mode register config applied (%zu registers)", kTmodeConfigSize);
    ESP_LOGI(TAG, "CC1101 sync word: 0x%02X%02X", sync1, sync0);
    return common::Result<void>::ok();
}

int8_t RadioCc1101::convert_rssi(uint8_t raw_rssi) {
    // CC1101 RSSI conversion per datasheet
    int16_t rssi;
    if (raw_rssi >= 128) {
        rssi = static_cast<int16_t>(raw_rssi) - 256;
    } else {
        rssi = static_cast<int16_t>(raw_rssi);
    }
    rssi = rssi / 2 - 74; // CC1101 offset
    return static_cast<int8_t>(rssi);
}

common::Result<uint8_t> RadioCc1101::read_marcstate() {
    uint8_t marcstate = 0;
    if (!spi_read_register(registers::MARCSTATE | registers::READ_BURST, marcstate)) {
        return common::Result<uint8_t>::error(common::ErrorCode::RadioSpiError);
    }
    return common::Result<uint8_t>::ok(static_cast<uint8_t>(marcstate & 0x1F));
}

#endif // HOST_TEST_BUILD

} // namespace radio_cc1101
