#pragma once

#include "protocol_driver/protocol_ids.hpp"

#include <cstdint>

// RadioProfileManager: per-radio-instance runtime object that tracks the
// active CC1101 profile and scheduler state for one radio.
//
// Responsibilities:
//   - Store which profile is currently active.
//   - Store the scheduler mode and enabled-profile mask (set once from config).
//   - Track the reason for the last profile switch and a switch counter.
//   - Track IRQ-first vs fallback-poll wake counts for diagnostics.
//   - Provide advance() for scheduled profile rotation (Scan / Priority modes).
//
// Multi-radio design:
//   Use for_instance(RadioInstanceId) to get the manager for a given radio.
//   instance() is a convenience alias for for_instance(kRadioInstancePrimary).
//   Each radio instance has an independent manager with independent state so
//   profile switching on one radio does not disturb the other.
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
    None                 = 0,
    Initial              = 1, // First profile set at startup (normal config)
    SchedulerAdvance     = 2, // Normal scan / priority advance
    FallbackTimeout      = 3, // Priority mode: timed out on preferred, fell back
    ManualOverride       = 4, // Future: operator-requested profile change
    PriosExperimentalLock = 5, // Scheduler overridden by PRIOS experimental mode
    PriorityReturn       = 6, // Priority mode: return to preferred profile
};

inline const char* scheduler_switch_reason_to_string(SchedulerSwitchReason r) {
    switch (r) {
        case SchedulerSwitchReason::Initial:               return "Initial";
        case SchedulerSwitchReason::SchedulerAdvance:      return "SchedulerAdvance";
        case SchedulerSwitchReason::FallbackTimeout:       return "FallbackTimeout";
        case SchedulerSwitchReason::ManualOverride:        return "ManualOverride";
        case SchedulerSwitchReason::PriosExperimentalLock: return "PriosExperimentalLock";
        case SchedulerSwitchReason::PriorityReturn:        return "PriorityReturn";
        default:                                           return "None";
    }
}

enum class ProfileApplyStatus : uint8_t {
    Pending = 0,
    Applied = 1,
    FallbackApplied = 2,
    ApplyFailed = 3,
    UnsupportedRequestedProfile = 4,
};

inline const char* profile_apply_status_to_string(ProfileApplyStatus status) {
    switch (status) {
        case ProfileApplyStatus::Pending:                   return "Pending";
        case ProfileApplyStatus::Applied:                   return "Applied";
        case ProfileApplyStatus::FallbackApplied:           return "FallbackApplied";
        case ProfileApplyStatus::ApplyFailed:               return "ApplyFailed";
        case ProfileApplyStatus::UnsupportedRequestedProfile:return "UnsupportedRequestedProfile";
        default:                                            return "Pending";
    }
}

enum class RuntimeWakeSource : uint8_t {
    None = 0,
    IrqNotification = 1,
    FallbackPoll = 2,
};

inline const char* runtime_wake_source_to_string(RuntimeWakeSource source) {
    switch (source) {
        case RuntimeWakeSource::IrqNotification: return "IrqNotification";
        case RuntimeWakeSource::FallbackPoll:    return "FallbackPoll";
        default:                                 return "None";
    }
}

class RadioProfileManager {
  public:
    // Access the manager for a specific radio instance.
    // Only kRadioInstancePrimary (0) and kRadioInstanceSecondary (1) are
    // supported.  Returns the primary instance for any out-of-range value.
    static RadioProfileManager& for_instance(RadioInstanceId radio_instance);

    // Convenience alias: returns the primary-radio manager.
    static RadioProfileManager& instance() {
        return for_instance(kRadioInstancePrimary);
    }

    // Called once from the radio owner task setup, before the receive loop.
    // Applies mode, enabled_profiles, and records which radio instance this
    // manager belongs to.  Sets the initial active profile to the first
    // enabled profile in priority order and records initial_reason.
    //
    // initial_reason defaults to Initial (normal startup). Pass
    // PriosExperimentalLock when an experimental PRIOS mode forces the profile
    // regardless of the user's scheduler config.
    void configure(RadioSchedulerMode mode, RadioProfileMask enabled_profiles,
                   RadioInstanceId radio_instance = kRadioInstancePrimary,
                   SchedulerSwitchReason initial_reason = SchedulerSwitchReason::Initial);

    // Snapshot for API / diagnostics — safe to call from any task.
    struct Status {
        RadioInstanceId       radio_instance       = kRadioInstancePrimary;
        RadioProfileId        preferred_profile_id = RadioProfileId::Unknown;
        RadioProfileId        selected_profile_id  = RadioProfileId::Unknown;
        RadioProfileId        active_profile_id    = RadioProfileId::Unknown;
        RadioSchedulerMode    scheduler_mode       = RadioSchedulerMode::Locked;
        SchedulerSwitchReason last_switch_reason   = SchedulerSwitchReason::None;
        ProfileApplyStatus    last_apply_status    = ProfileApplyStatus::Pending;
        RuntimeWakeSource     last_wake_source     = RuntimeWakeSource::None;
        RadioProfileMask      enabled_profiles     = kRadioProfileMaskNone;
        uint32_t              profile_switch_count = 0;
        uint32_t              profile_apply_count  = 0;
        uint32_t              profile_apply_failures = 0;
        uint32_t              irq_wake_count       = 0;
        uint32_t              fallback_wake_count  = 0;
    };

    Status         status() const;
    RadioProfileId active_profile_id() const;
    RadioProfileId selected_profile_id() const;
    RadioProfileId preferred_profile_id() const;

    // Advance to the next enabled profile according to the current mode.
    // In Locked mode this is a no-op. In Scan/Priority mode it returns the
    // next profile to apply and updates internal state.
    // Called by the radio owner task between receive windows.
    RadioProfileId advance(SchedulerSwitchReason reason = SchedulerSwitchReason::SchedulerAdvance);
    void note_profile_applied(RadioProfileId applied_profile,
                              ProfileApplyStatus status = ProfileApplyStatus::Applied);
    void note_profile_apply_failure(
        ProfileApplyStatus status = ProfileApplyStatus::ApplyFailed);

    // Wake source accounting — called by the radio owner task per wake event.
    void record_irq_wake();
    void record_fallback_wake();

  private:
    RadioProfileManager() = default;

    RadioInstanceId       radio_instance_id_    = kRadioInstancePrimary;
    RadioProfileId        preferred_profile_id_ = RadioProfileId::WMbusT868;
    RadioProfileId        selected_profile_id_  = RadioProfileId::WMbusT868;
    RadioProfileId        applied_profile_id_   = RadioProfileId::Unknown;
    RadioSchedulerMode    scheduler_mode_       = RadioSchedulerMode::Locked;
    SchedulerSwitchReason last_switch_reason_   = SchedulerSwitchReason::None;
    ProfileApplyStatus    last_apply_status_    = ProfileApplyStatus::Pending;
    RuntimeWakeSource     last_wake_source_     = RuntimeWakeSource::None;
    RadioProfileMask      enabled_profiles_     = kRadioProfileMaskWMbusT868;
    uint32_t              profile_switch_count_ = 0;
    uint32_t              profile_apply_count_  = 0;
    uint32_t              profile_apply_failures_ = 0;
    uint32_t              irq_wake_count_       = 0;
    uint32_t              fallback_wake_count_  = 0;

    // Return the first enabled profile in canonical order (T868 → PriosR3 → PriosR4).
    RadioProfileId first_enabled_profile() const;

    // Return the next enabled profile after `current` in circular order.
    RadioProfileId next_enabled_profile(RadioProfileId current) const;
};

} // namespace protocol_driver
