#pragma once

#include "radio_cc1101/cc1101_owner_events.hpp"
#include "radio_cc1101/cc1101_irq.hpp"
#include "common/error.hpp"
#include "common/result.hpp"
#include <cstddef>
#include <cstdint>
#include <mutex>

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

enum class RadioDropReason : uint8_t {
    None = 0,
    OversizedBurst,
    BurstTimeout,
};

enum class RadioBurstEndReason : uint8_t {
    None = 0,
    EmptyPolls,
    MaxDuration,
};

enum class RawBurstTimeoutAction : uint8_t {
    Continue = 0,
    ReturnNotFound,
    EndBurst,
};

RawBurstTimeoutAction raw_burst_timeout_action(uint32_t elapsed_ticks, uint16_t received,
                                               uint32_t max_duration_ticks);

struct RadioDropInfo {
    RadioDropReason reason = RadioDropReason::None;
    uint16_t captured_length = 0;
    uint16_t elapsed_ms = 0;
    uint8_t first_data_byte = 0;
    uint8_t prefix[8]{};
    uint8_t prefix_length = 0;
    bool quality_issue = false;
};

struct RawRadioFrame {
    static constexpr size_t MAX_DATA_SIZE = 290;

    // Exact data bytes drained from the CC1101 RX FIFO for one raw receive burst.
    // In the current raw-capture contract there is no packet-length prefix semantics:
    // the whole buffer is the radio payload that should be handed to the pipeline.
    uint8_t data[MAX_DATA_SIZE]{};
    uint16_t length = 0;
    uint16_t payload_offset = 0;
    uint16_t payload_length = 0;
    uint16_t capture_elapsed_ms = 0;
    uint8_t first_data_byte = 0;
    RadioBurstEndReason burst_end_reason = RadioBurstEndReason::None;
    int8_t rssi_dbm = 0;
    uint8_t lqi = 0;
    bool crc_ok = false;
    bool radio_crc_available = false;
};

struct RxFifoStatus {
    uint8_t fifo_bytes = 0;
    bool fifo_overflow = false;
    uint8_t marcstate = 0;
    bool receiving = false;
};

struct RxSignalQuality {
    int8_t rssi_dbm = 0;
    uint8_t lqi = 0;
    bool crc_ok = false;
    bool radio_crc_available = false;
};

enum class RadioPacketLengthMode : uint8_t {
    Infinite = 0,
    FixedLength,
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
    // - Success: one raw receive burst was drained from the FIFO. RawRadioFrame contains the exact
    //   radio bytes that should be handed to the pipeline, without CC1101 length-prefix semantics.
    // - Expected idle: NotFound means no complete frame is currently available.
    // - Quality drop: RadioQualityDrop means a burst was seen but rejected due to framing /
    //   boundary issues such as timeout or oversize capture.
    // - Soft failure: Timeout and RadioSpiError still represent RX-path problems that the caller
    //   may keep alive or escalate after repeated occurrences.
    // - Recovery trigger: FIFO overflow returns RadioFifoOverflow immediately; repeated soft
    //   failures are escalated by RadioStateMachine.
    // - Hardware gap: burst traffic, FIFO timing margins, and poll-vs-interrupt trade-offs still
    //   require real ESP32 + CC1101 validation.
    //
    // Read one raw receive burst from the RX FIFO. Long frames are drained in <=64 B bursts
    // with a bounded wait; burst end is detected by an inter-byte idle gap while RX remains active.
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
    RadioDropInfo last_drop() const;

    // Read chip part number and version for identification
    bool verify_chip_id();
    common::Result<void> claim_owner(void* owner_token);
    void release_owner(void* owner_token);
    [[nodiscard]] bool is_owned_by(void* owner_token) const;
    common::Result<void> enable_gdo_interrupts(void* owner_token, void* owner_task_handle);
    void disable_gdo_interrupts();
    [[nodiscard]] GdoIrqSnapshot take_gdo_irq_snapshot();
    [[nodiscard]] RadioOwnerEventSet take_owner_events();
    common::Result<RxFifoStatus> owner_read_rx_status(void* owner_token);
    common::Result<uint16_t> owner_read_fifo_bytes(void* owner_token, uint8_t* buffer,
                                                   uint16_t capacity);
    common::Result<RxSignalQuality> owner_read_signal_quality(void* owner_token);
    common::Result<void> owner_switch_to_fixed_length_capture(void* owner_token,
                                                              uint8_t remaining_encoded_bytes);
    common::Result<void> owner_restore_infinite_packet_mode(void* owner_token);
    common::Result<void> owner_abort_and_restart_rx(void* owner_token);

  private:
    RadioCc1101() = default;

    bool initialized_ = false;
    RadioState state_ = RadioState::Uninitialized;
    RadioCounters counters_{};
    mutable std::mutex last_drop_mutex_{};
    RadioDropInfo last_drop_{};
    SpiPins pins_{};
    SpiBusConfig bus_config_{};
    GdoIrqTracker irq_tracker_{};
    RadioOwnerClaimState owner_claim_{};
    bool irq_plumbing_enabled_ = false;
    void* spi_handle_ = nullptr; // spi_device_handle_t
};

} // namespace radio_cc1101
