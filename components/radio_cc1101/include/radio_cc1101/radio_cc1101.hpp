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

    // Exact data bytes drained from the CC1101 RX FIFO, excluding the appended status bytes.
    // In the current variable-length packet-engine contract this includes:
    // - data[0]: CC1101 packet length prefix
    // - data[payload_offset .. payload_offset + payload_length): packet payload bytes
    //
    // This buffer is intentionally not "already decoded WMBus". It is the radio-layer packet as
    // delivered by the CC1101 packet engine.
    uint8_t data[MAX_DATA_SIZE]{};
    uint16_t length = 0;
    uint16_t payload_offset = 0;
    uint16_t payload_length = 0;
    int8_t rssi_dbm = 0;
    uint8_t lqi = 0;
    bool crc_ok = false;
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

    // Polling-mode RX contract:
    // - Success: a complete CC1101 packet plus trailing RSSI/LQI/CRC status was drained from the
    //   FIFO. RawRadioFrame contains the exact packet bytes emitted by the packet engine, not a
    //   decoded Wireless M-Bus frame.
    // - Soft failure: NotFound means no complete frame is currently available; Timeout and
    //   InvalidArgument mean polling observed partial/invalid FIFO state and the caller may keep
    //   RX alive or escalate after repeated occurrences.
    // - Recovery trigger: FIFO overflow returns RadioFifoOverflow immediately; repeated soft
    //   failures are escalated by RadioStateMachine.
    // - Hardware gap: burst traffic, FIFO timing margins, and poll-vs-interrupt trade-offs still
    //   require real ESP32 + CC1101 validation.
    //
    // Read one packet-engine frame from the RX FIFO. Long frames are drained in <=64 B bursts
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
    bool spi_strobe(uint8_t strobe_addr, uint8_t* chip_status = nullptr);
    bool spi_read_register(uint8_t addr, uint8_t& value);
    bool spi_write_register(uint8_t addr, uint8_t value);
    bool spi_read_burst(uint8_t addr, uint8_t* buffer, size_t length);

    common::Result<void> apply_tmode_config();
    int8_t convert_rssi(uint8_t raw_rssi);
    common::Result<uint8_t> read_marcstate();

    void* spi_handle_ = nullptr; // spi_device_handle_t
#endif

    bool initialized_ = false;
    RadioState state_ = RadioState::Uninitialized;
    RadioCounters counters_{};
    SpiPins pins_{};
    SpiBusConfig bus_config_{};
};

} // namespace radio_cc1101
