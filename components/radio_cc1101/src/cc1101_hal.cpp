#include "cc1101_hal.hpp"

#include "radio_cc1101/cc1101_registers.hpp"

#include <cstring>

#ifndef HOST_TEST_BUILD
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"

namespace {
constexpr const char* TAG = "cc1101_hal";
}
#endif

namespace radio_cc1101::hal {

common::Result<void> initialize_spi_device(void*& spi_handle, const SpiPins& pins,
                                           const SpiBusConfig& bus_config) {
#ifndef HOST_TEST_BUILD
    spi_bus_config_t bus_cfg{};
    bus_cfg.mosi_io_num = pins.mosi;
    bus_cfg.miso_io_num = pins.miso;
    bus_cfg.sclk_io_num = pins.sck;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = bus_config.max_transfer_size;

    const spi_host_device_t spi_host = static_cast<spi_host_device_t>(bus_config.host_id);
    bool spi_bus_initialized = false;

    const auto cleanup_spi = [&]() {
        if (spi_handle) {
            spi_bus_remove_device(static_cast<spi_device_handle_t>(spi_handle));
            spi_handle = nullptr;
        }
        if (spi_bus_initialized) {
            spi_bus_free(spi_host);
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
                             reinterpret_cast<spi_device_handle_t*>(&spi_handle));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %d", err);
        cleanup_spi();
        return common::Result<void>::error(common::ErrorCode::RadioSpiError);
    }
    return common::Result<void>::ok();
#else
    (void)spi_handle;
    (void)pins;
    (void)bus_config;
    return common::Result<void>::ok();
#endif
}

void deinitialize_spi_device(void*& spi_handle, int host_id) {
#ifndef HOST_TEST_BUILD
    if (spi_handle) {
        spi_bus_remove_device(static_cast<spi_device_handle_t>(spi_handle));
        spi_handle = nullptr;
    }
    spi_bus_free(static_cast<spi_host_device_t>(host_id));
#else
    (void)spi_handle;
    (void)host_id;
#endif
}

common::Result<void> configure_input_pin(int pin) {
#ifndef HOST_TEST_BUILD
    if (pin < 0) {
        return common::Result<void>::ok();
    }
    const esp_err_t err =
        gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        return common::Result<void>::error(common::ErrorCode::RadioInitFailed);
    }
    return common::Result<void>::ok();
#else
    (void)pin;
    return common::Result<void>::ok();
#endif
}

bool spi_strobe(void* spi_handle, uint8_t strobe_addr, uint8_t* chip_status) {
#ifndef HOST_TEST_BUILD
    spi_transaction_t txn{};
    txn.length = 8;
    txn.tx_data[0] = strobe_addr;
    txn.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
    const esp_err_t err = spi_device_transmit(static_cast<spi_device_handle_t>(spi_handle), &txn);
    if (err != ESP_OK) {
        return false;
    }
    if (chip_status) {
        *chip_status = txn.rx_data[0];
    }
    return true;
#else
    (void)spi_handle;
    (void)strobe_addr;
    if (chip_status) {
        *chip_status = 0;
    }
    return true;
#endif
}

bool spi_read_register(void* spi_handle, uint8_t addr, uint8_t& value) {
#ifndef HOST_TEST_BUILD
    spi_transaction_t txn{};
    txn.length = 16;
    txn.tx_data[0] = addr | registers::READ_SINGLE;
    txn.tx_data[1] = 0x00;
    txn.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
    const esp_err_t err = spi_device_transmit(static_cast<spi_device_handle_t>(spi_handle), &txn);
    if (err != ESP_OK) {
        return false;
    }
    value = txn.rx_data[1];
    return true;
#else
    (void)spi_handle;
    (void)addr;
    value = 0;
    return true;
#endif
}

bool spi_write_register(void* spi_handle, uint8_t addr, uint8_t value) {
#ifndef HOST_TEST_BUILD
    spi_transaction_t txn{};
    txn.length = 16;
    txn.tx_data[0] = addr;
    txn.tx_data[1] = value;
    txn.flags = SPI_TRANS_USE_TXDATA;
    return spi_device_transmit(static_cast<spi_device_handle_t>(spi_handle), &txn) == ESP_OK;
#else
    (void)spi_handle;
    (void)addr;
    (void)value;
    return true;
#endif
}

bool spi_read_burst(void* spi_handle, uint8_t addr, uint8_t* buffer, size_t length) {
    if (!buffer || length == 0U) {
        return false;
    }
#ifndef HOST_TEST_BUILD
    uint8_t tx_buf[65]{};
    uint8_t rx_buf[65]{};
    tx_buf[0] = addr;
    spi_transaction_t txn{};
    txn.length = static_cast<uint32_t>((length + 1U) * 8U);
    txn.tx_buffer = tx_buf;
    txn.rx_buffer = rx_buf;
    const esp_err_t err = spi_device_transmit(static_cast<spi_device_handle_t>(spi_handle), &txn);
    if (err != ESP_OK) {
        return false;
    }
    std::memcpy(buffer, &rx_buf[1], length);
    return true;
#else
    (void)spi_handle;
    (void)addr;
    for (size_t i = 0; i < length; ++i) {
        buffer[i] = 0U;
    }
    return true;
#endif
}

} // namespace radio_cc1101::hal
