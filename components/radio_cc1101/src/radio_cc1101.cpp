#include "radio_cc1101/radio_cc1101.hpp"
#include "radio_cc1101/cc1101_registers.hpp"

#ifndef HOST_TEST_BUILD
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char* TAG = "radio_cc1101";
#endif

namespace radio_cc1101 {

RadioCc1101& RadioCc1101::instance() {
    static RadioCc1101 radio;
    return radio;
}

common::Result<void> RadioCc1101::initialize(const SpiPins& pins) {
    if (initialized_) {
        return common::Result<void>::error(common::ErrorCode::AlreadyInitialized);
    }

    pins_ = pins;

#ifndef HOST_TEST_BUILD
    // Configure SPI bus
    spi_bus_config_t bus_cfg{};
    bus_cfg.mosi_io_num = pins.mosi;
    bus_cfg.miso_io_num = pins.miso;
    bus_cfg.sclk_io_num = pins.sck;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 64;

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %d", err);
        return common::Result<void>::error(common::ErrorCode::RadioSpiError);
    }

    spi_device_interface_config_t dev_cfg{};
    dev_cfg.clock_speed_hz = 4 * 1000 * 1000; // 4 MHz SPI clock
    dev_cfg.mode = 0;
    dev_cfg.spics_io_num = pins.cs;
    dev_cfg.queue_size = 1;

    err = spi_bus_add_device(SPI2_HOST,
                             &dev_cfg,
                             reinterpret_cast<spi_device_handle_t*>(&spi_handle_));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %d", err);
        return common::Result<void>::error(common::ErrorCode::RadioSpiError);
    }

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
        return result;
    }

    if (!verify_chip_id()) {
        ESP_LOGE(TAG, "CC1101 chip ID verification failed");
        return common::Result<void>::error(common::ErrorCode::RadioInitFailed);
    }

    auto config_result = apply_tmode_config();
    if (config_result.is_error()) {
        return config_result;
    }

    ESP_LOGI(TAG, "CC1101 initialized for T-mode 868.95 MHz");
#endif

    state_ = RadioState::Idle;
    initialized_ = true;
    return common::Result<void>::ok();
}

common::Result<void> RadioCc1101::reset() {
#ifndef HOST_TEST_BUILD
    spi_strobe(registers::SRES);
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
    spi_strobe(registers::SRX);
    vTaskDelay(pdMS_TO_TICKS(1));

    uint8_t marc = read_marcstate();
    if (marc != registers::MARCSTATE_RX) {
        ESP_LOGW(TAG, "Expected MARCSTATE_RX, got 0x%02X", marc);
    }
#endif

    state_ = RadioState::Receiving;
    return common::Result<void>::ok();
}

common::Result<void> RadioCc1101::go_idle() {
#ifndef HOST_TEST_BUILD
    spi_strobe(registers::SIDLE);
#endif
    state_ = RadioState::Idle;
    return common::Result<void>::ok();
}

common::Result<RawRadioFrame> RadioCc1101::read_frame() {
    if (!initialized_) {
        return common::Result<RawRadioFrame>::error(
            common::ErrorCode::NotInitialized);
    }

#ifndef HOST_TEST_BUILD
    // Check MARCSTATE for FIFO overflow
    uint8_t marc = read_marcstate();
    if (marc == registers::MARCSTATE_RXFIFO_OVERFLOW) {
        counters_.fifo_overflows++;
        ESP_LOGW(TAG, "RX FIFO overflow detected");
        flush_rx_fifo();
        spi_strobe(registers::SRX);
        state_ = RadioState::Error;
        return common::Result<RawRadioFrame>::error(
            common::ErrorCode::RadioFifoOverflow);
    }

    // Read number of bytes in RX FIFO
    uint8_t rx_bytes = spi_read_register(registers::RXBYTES | registers::READ_BURST);
    uint8_t num_bytes = rx_bytes & 0x7F; // Mask off overflow bit
    bool overflow = (rx_bytes & 0x80) != 0;

    if (overflow) {
        counters_.fifo_overflows++;
        flush_rx_fifo();
        spi_strobe(registers::SRX);
        return common::Result<RawRadioFrame>::error(
            common::ErrorCode::RadioFifoOverflow);
    }

    if (num_bytes < 3) {
        // Not enough data for a meaningful frame (need at least length + 2 status)
        return common::Result<RawRadioFrame>::error(
            common::ErrorCode::NotFound);
    }

    RawRadioFrame frame{};

    // Read the first byte as packet length (T-mode convention)
    uint8_t pkt_len = spi_read_register(registers::FIFO_RX | registers::READ_SINGLE);
    if (pkt_len == 0 || pkt_len > RawRadioFrame::MAX_DATA_SIZE - 1) {
        flush_rx_fifo();
        return common::Result<RawRadioFrame>::error(
            common::ErrorCode::InvalidArgument);
    }

    // The L-field is the first byte of the frame
    frame.data[0] = pkt_len;
    frame.length = pkt_len + 1; // +1 for the L-field itself

    // Read remaining frame bytes
    uint16_t to_read = pkt_len; // Remaining bytes after L-field
    if (to_read > registers::FIFO_SIZE - 3) {
        to_read = registers::FIFO_SIZE - 3;
    }
    if (to_read > 0) {
        spi_read_burst(registers::FIFO_RX | registers::READ_BURST,
                       &frame.data[1], to_read);
    }

    // Read appended status bytes (RSSI, LQI|CRC_OK)
    uint8_t status[2];
    spi_read_burst(registers::FIFO_RX | registers::READ_BURST, status, 2);

    frame.rssi_dbm = convert_rssi(status[0]);
    frame.lqi = status[1] & 0x7F;
    frame.crc_ok = (status[1] & 0x80) != 0;

    counters_.frames_received++;
    if (frame.crc_ok) {
        counters_.frames_crc_ok++;
    } else {
        counters_.frames_crc_fail++;
    }

    return common::Result<RawRadioFrame>::ok(frame);
#else
    // Host test stub: return a synthetic frame
    RawRadioFrame frame{};
    frame.data[0] = 0x2C;
    frame.data[1] = 0x44;
    frame.data[2] = 0x93;
    frame.length = 3;
    frame.rssi_dbm = -65;
    frame.lqi = 45;
    frame.crc_ok = true;
    counters_.frames_received++;
    counters_.frames_crc_ok++;
    return common::Result<RawRadioFrame>::ok(frame);
#endif
}

common::Result<void> RadioCc1101::flush_rx_fifo() {
#ifndef HOST_TEST_BUILD
    spi_strobe(registers::SIDLE);
    spi_strobe(registers::SFRX);
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
    uint8_t partnum = spi_read_register(registers::PARTNUM | registers::READ_BURST);
    uint8_t version = spi_read_register(registers::VERSION | registers::READ_BURST);

    ESP_LOGI(TAG, "CC1101 PARTNUM=0x%02X VERSION=0x%02X", partnum, version);

    return (partnum == registers::CC1101_PARTNUM &&
            version == registers::CC1101_VERSION_EXPECTED);
#else
    return true;
#endif
}

#ifndef HOST_TEST_BUILD

uint8_t RadioCc1101::spi_strobe(uint8_t strobe_addr) {
    spi_transaction_t txn{};
    txn.length = 8;
    txn.tx_data[0] = strobe_addr;
    txn.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;

    esp_err_t err = spi_device_transmit(
        static_cast<spi_device_handle_t>(spi_handle_), &txn);
    if (err != ESP_OK) {
        counters_.spi_errors++;
    }
    return txn.rx_data[0];
}

uint8_t RadioCc1101::spi_read_register(uint8_t addr) {
    spi_transaction_t txn{};
    txn.length = 16;
    txn.tx_data[0] = addr | registers::READ_SINGLE;
    txn.tx_data[1] = 0x00;
    txn.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;

    esp_err_t err = spi_device_transmit(
        static_cast<spi_device_handle_t>(spi_handle_), &txn);
    if (err != ESP_OK) {
        counters_.spi_errors++;
        return 0;
    }
    return txn.rx_data[1];
}

void RadioCc1101::spi_write_register(uint8_t addr, uint8_t value) {
    spi_transaction_t txn{};
    txn.length = 16;
    txn.tx_data[0] = addr;
    txn.tx_data[1] = value;
    txn.flags = SPI_TRANS_USE_TXDATA;

    esp_err_t err = spi_device_transmit(
        static_cast<spi_device_handle_t>(spi_handle_), &txn);
    if (err != ESP_OK) {
        counters_.spi_errors++;
    }
}

void RadioCc1101::spi_read_burst(uint8_t addr, uint8_t* buffer, size_t length) {
    if (!buffer || length == 0) return;

    uint8_t tx_buf[65]{};
    uint8_t rx_buf[65]{};
    tx_buf[0] = addr;

    spi_transaction_t txn{};
    txn.length = (length + 1) * 8;
    txn.tx_buffer = tx_buf;
    txn.rx_buffer = rx_buf;

    esp_err_t err = spi_device_transmit(
        static_cast<spi_device_handle_t>(spi_handle_), &txn);
    if (err != ESP_OK) {
        counters_.spi_errors++;
        return;
    }
    std::memcpy(buffer, &rx_buf[1], length);
}

common::Result<void> RadioCc1101::apply_tmode_config() {
    for (size_t i = 0; i < kTmodeConfigSize; ++i) {
        spi_write_register(kTmodeConfig[i].addr, kTmodeConfig[i].value);
    }
    ESP_LOGI(TAG, "T-mode register config applied (%zu registers)",
             kTmodeConfigSize);
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

uint8_t RadioCc1101::read_marcstate() {
    return spi_read_register(registers::MARCSTATE | registers::READ_BURST) & 0x1F;
}

#endif // HOST_TEST_BUILD

} // namespace radio_cc1101
