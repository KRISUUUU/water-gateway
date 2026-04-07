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
    uint32_t fifo_overflows = 0;
    uint32_t radio_resets = 0;
    uint32_t radio_recoveries = 0;
    uint32_t spi_errors = 0;
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
    common::Result<void> owner_apply_tmode_profile(void* owner_token);

    // Apply PRIOS R3 register profile while holding radio ownership.
    // Transitions: idle → configure registers → flush FIFO → RX-ready.
    // manchester_enabled=false → Variant A; manchester_enabled=true → Variant B.
    // Both variants are EXPERIMENTAL. Call only when the sync-based PRIOS
    // campaign mode is active; otherwise the radio remains in T-mode
    // configuration.
    common::Result<void> owner_apply_prios_r3_profile(void* owner_token,
                                                       bool  manchester_enabled);
    // Discovery/sniffer profile for PRIOS evidence gathering without the
    // placeholder sync-word assumption.
    common::Result<void> owner_apply_prios_r3_discovery_profile(void* owner_token,
                                                                 bool  manchester_enabled);
    common::Result<void> owner_apply_prios_r4_profile(void* owner_token,
                                                      bool  manchester_enabled);
    common::Result<void> owner_apply_prios_r4_discovery_profile(void* owner_token,
                                                                bool  manchester_enabled);

  private:
    RadioCc1101() = default;

    bool initialized_ = false;
    RadioState state_ = RadioState::Uninitialized;
    RadioCounters counters_{};
    SpiPins pins_{};
    SpiBusConfig bus_config_{};
    GdoIrqTracker irq_tracker_{};
    RadioOwnerClaimState owner_claim_{};
    bool irq_plumbing_enabled_ = false;
    void* spi_handle_ = nullptr; // spi_device_handle_t
};

} // namespace radio_cc1101
