#pragma once

#include "protocol_driver/protocol_ids.hpp"

#include <cstdint>

// RadioProfileManager: runtime singleton that tracks the active CC1101 profile
// and scheduler state for one radio instance.
//
// Responsibilities:
//   - Store which profile is currently active.
//   - Store the scheduler mode and enabled-profile mask (set once from config).
//   - Track the reason for the last profile switch and a switch counter.
//   - Track IRQ-first vs fallback-poll wake counts for diagnostics.
//   - Provide advance() for scheduled profile rotation (Scan / Priority modes).
//
// Non-responsibilities:
//   - No SPI / GPIO. Profile switching logic lives in the radio owner task.
//   - No FreeRTOS. The radio task is the sole writer of mutable fields.
//
// Thread safety: The radio owner task writes; the HTTP task reads via status().
// Diagnostic counters are read-only snapshots so exact coherency is not
// required — the same pattern as RadioStateMachine.

namespace protocol_driver {

enum class SchedulerSwitchReason : uint8_t {
    None             = 0,
    Initial          = 1, // First profile set at startup
    SchedulerAdvance = 2, // Normal scan / priority advance
    FallbackTimeout  = 3, // Priority mode: timed out on preferred, fell back
    ManualOverride   = 4, // Future: operator-requested profile change
};

inline const char* scheduler_switch_reason_to_string(SchedulerSwitchReason r) {
    switch (r) {
        case SchedulerSwitchReason::Initial:          return "Initial";
        case SchedulerSwitchReason::SchedulerAdvance: return "SchedulerAdvance";
        case SchedulerSwitchReason::FallbackTimeout:  return "FallbackTimeout";
        case SchedulerSwitchReason::ManualOverride:   return "ManualOverride";
        default:                                      return "None";
    }
}

class RadioProfileManager {
  public:
    static RadioProfileManager& instance();

    // Called once from the radio owner task setup, before the receive loop.
    // Applies mode and enabled_profiles from AppConfig; sets the initial
    // active profile to the first enabled profile in priority order.
    void configure(RadioSchedulerMode mode, RadioProfileMask enabled_profiles);

    // Snapshot for API / diagnostics — safe to call from any task.
    struct Status {
        RadioProfileId        active_profile_id    = RadioProfileId::Unknown;
        RadioSchedulerMode    scheduler_mode       = RadioSchedulerMode::Locked;
        SchedulerSwitchReason last_switch_reason   = SchedulerSwitchReason::None;
        RadioProfileMask      enabled_profiles     = kRadioProfileMaskNone;
        uint32_t              profile_switch_count = 0;
        uint32_t              irq_wake_count       = 0;
        uint32_t              fallback_wake_count  = 0;
    };

    Status         status() const;
    RadioProfileId active_profile_id() const;

    // Advance to the next enabled profile according to the current mode.
    // In Locked mode this is a no-op. In Scan/Priority mode it returns the
    // next profile to apply and updates internal state.
    // Called by the radio owner task between receive windows.
    RadioProfileId advance(SchedulerSwitchReason reason = SchedulerSwitchReason::SchedulerAdvance);

    // Wake source accounting — called by the radio owner task per wake event.
    void record_irq_wake();
    void record_fallback_wake();

  private:
    RadioProfileManager() = default;

    RadioProfileId        active_profile_id_    = RadioProfileId::WMbusT868;
    RadioSchedulerMode    scheduler_mode_       = RadioSchedulerMode::Locked;
    SchedulerSwitchReason last_switch_reason_   = SchedulerSwitchReason::None;
    RadioProfileMask      enabled_profiles_     = kRadioProfileMaskWMbusT868;
    uint32_t              profile_switch_count_ = 0;
    uint32_t              irq_wake_count_       = 0;
    uint32_t              fallback_wake_count_  = 0;

    // Return the first enabled profile in canonical order (T868 → PriosR3 → PriosR4).
    RadioProfileId first_enabled_profile() const;

    // Return the next enabled profile after `current` in circular order.
    RadioProfileId next_enabled_profile(RadioProfileId current) const;
};

} // namespace protocol_driver
