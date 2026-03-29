#pragma once

#include "common/error.hpp"
#include "common/result.hpp"
#include <cstddef>
#include <cstdint>

namespace radio_cc1101 {

enum class RadioState : uint8_t {
    Uninitialized = 0,
    Idle,
    Receiving,
    Error,
};

struct RadioCounters {
    uint32_t rx_read_calls = 0;
    uint32_t rx_not_found = 0;
    uint32_t rx_timeouts = 0;
    uint32_t frames_received = 0;
    uint32_t frames_crc_ok = 0;
    uint32_t frames_crc_fail = 0;
    uint32_t frames_incomplete = 0;
    uint32_t frames_dropped_too_long = 0;
    uint32_t fifo_overflows = 0;
    uint32_t radio_resets = 0;
    uint32_t radio_recoveries = 0;
    uint32_t spi_errors = 0;
};

struct RawRadioFrame {
    static constexpr size_t MAX_DATA_SIZE = 290;

    uint8_t data[MAX_DATA_SIZE];
    uint16_t length;
    int8_t rssi_dbm;
    uint8_t lqi;
    bool crc_ok;
};

// SPI pin configuration
struct SpiPins {
    int mosi;
    int miso;
    int sck;
    int cs;
    int gdo0; // Optional: -1 if not used
    int gdo2; // Optional: -1 if not used
};

struct SpiBusConfig {
    int host_id = 2; // SPI2_HOST on ESP32
    uint32_t clock_hz = 4 * 1000 * 1000;
    int max_transfer_size = 64;
};

class RadioCc1101 {
  public:
    static RadioCc1101& instance();

    // Initialize SPI bus and CC1101 chip with T-mode config.
    // Must provide valid SPI pins.
    common::Result<void> initialize(const SpiPins& pins);
    common::Result<void> initialize(const SpiPins& pins, const SpiBusConfig& bus_config);

    // Reset CC1101 and re-apply configuration
    common::Result<void> reset();

    // Enter RX mode
    common::Result<void> start_rx();

    // Return to idle
    common::Result<void> go_idle();

    // Read one WMBus T-mode frame from the RX FIFO. Long frames are drained in <=64 B bursts
    // with a bounded wait (no single-FIFO assumption); timeout flushes FIFO on stall.
    common::Result<RawRadioFrame> read_frame();

    // Flush RX FIFO (used during error recovery)
    common::Result<void> flush_rx_fifo();

    // Recovery: reset + reconfigure + restart RX
    common::Result<void> recover();

    RadioState state() const {
        return state_;
    }
    const RadioCounters& counters() const {
        return counters_;
    }

    // Read chip part number and version for identification
    bool verify_chip_id();

  private:
    RadioCc1101() = default;

#ifndef HOST_TEST_BUILD
    // Low-level SPI operations
    uint8_t spi_strobe(uint8_t strobe_addr);
    uint8_t spi_read_register(uint8_t addr);
    void spi_write_register(uint8_t addr, uint8_t value);
    void spi_read_burst(uint8_t addr, uint8_t* buffer, size_t length);

    common::Result<void> apply_tmode_config();
    int8_t convert_rssi(uint8_t raw_rssi);
    uint8_t read_marcstate();

    void* spi_handle_ = nullptr; // spi_device_handle_t
#endif

    bool initialized_ = false;
    RadioState state_ = RadioState::Uninitialized;
    RadioCounters counters_{};
    SpiPins pins_{};
    SpiBusConfig bus_config_{};
};

} // namespace radio_cc1101
