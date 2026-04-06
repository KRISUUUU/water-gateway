#include "protocol_driver/radio_profile_manager.hpp"

#include <cstddef>

namespace protocol_driver {

// Canonical profile order used by the advance() round-robin.
static constexpr RadioProfileId kProfileOrder[] = {
    RadioProfileId::WMbusT868,
    RadioProfileId::WMbusPriosR3,
    RadioProfileId::WMbusPriosR4,
};
static constexpr size_t kProfileOrderCount =
    sizeof(kProfileOrder) / sizeof(kProfileOrder[0]);

RadioProfileManager& RadioProfileManager::for_instance(RadioInstanceId radio_instance) {
    // Two static instances — one per supported radio.
    // Index 0 = primary, index 1 = secondary.
    static RadioProfileManager s_instances[2];
    const size_t idx = (radio_instance <= kRadioInstanceSecondary)
                           ? static_cast<size_t>(radio_instance)
                           : 0U;
    return s_instances[idx];
}

RadioProfileId RadioProfileManager::first_enabled_profile() const {
    for (size_t i = 0; i < kProfileOrderCount; ++i) {
        const RadioProfileMask bit =
            static_cast<RadioProfileMask>(1U << static_cast<uint8_t>(kProfileOrder[i]));
        if (enabled_profiles_ & bit) {
            return kProfileOrder[i];
        }
    }
    return RadioProfileId::WMbusT868; // Fallback: T-mode is always safe
}

RadioProfileId RadioProfileManager::next_enabled_profile(RadioProfileId current) const {
    // Find index of current in the order table.
    size_t current_idx = 0;
    for (size_t i = 0; i < kProfileOrderCount; ++i) {
        if (kProfileOrder[i] == current) {
            current_idx = i;
            break;
        }
    }

    // Step forward until we find an enabled profile other than current.
    for (size_t tries = 1; tries <= kProfileOrderCount; ++tries) {
        const size_t idx = (current_idx + tries) % kProfileOrderCount;
        const RadioProfileMask bit =
            static_cast<RadioProfileMask>(1U << static_cast<uint8_t>(kProfileOrder[idx]));
        if (enabled_profiles_ & bit) {
            return kProfileOrder[idx];
        }
    }

    return current; // Only one profile enabled; no change
}

void RadioProfileManager::configure(RadioSchedulerMode    mode,
                                    RadioProfileMask      enabled_profiles,
                                    RadioInstanceId       radio_instance,
                                    SchedulerSwitchReason initial_reason) {
    radio_instance_id_ = radio_instance;
    scheduler_mode_   = mode;
    enabled_profiles_ = enabled_profiles != kRadioProfileMaskNone
                            ? enabled_profiles
                            : kRadioProfileMaskWMbusT868;
    preferred_profile_id_ = first_enabled_profile();
    selected_profile_id_  = preferred_profile_id_;
    applied_profile_id_   = RadioProfileId::Unknown;
    last_switch_reason_   = initial_reason;
    last_apply_status_    = ProfileApplyStatus::Pending;
    last_wake_source_     = RuntimeWakeSource::None;
    profile_switch_count_ = 0;
    profile_apply_count_  = 0;
    profile_apply_failures_ = 0;
    irq_wake_count_       = 0;
    fallback_wake_count_  = 0;
}

RadioProfileManager::Status RadioProfileManager::status() const {
    Status s;
    s.radio_instance       = radio_instance_id_;
    s.preferred_profile_id = preferred_profile_id_;
    s.selected_profile_id  = selected_profile_id_;
    s.active_profile_id    = applied_profile_id_;
    s.scheduler_mode       = scheduler_mode_;
    s.last_switch_reason   = last_switch_reason_;
    s.last_apply_status    = last_apply_status_;
    s.last_wake_source     = last_wake_source_;
    s.enabled_profiles     = enabled_profiles_;
    s.profile_switch_count = profile_switch_count_;
    s.profile_apply_count  = profile_apply_count_;
    s.profile_apply_failures = profile_apply_failures_;
    s.irq_wake_count       = irq_wake_count_;
    s.fallback_wake_count  = fallback_wake_count_;
    return s;
}

RadioProfileId RadioProfileManager::active_profile_id() const {
    return applied_profile_id_;
}

RadioProfileId RadioProfileManager::selected_profile_id() const {
    return selected_profile_id_;
}

RadioProfileId RadioProfileManager::preferred_profile_id() const {
    return preferred_profile_id_;
}

RadioProfileId RadioProfileManager::advance(SchedulerSwitchReason reason) {
    if (scheduler_mode_ == RadioSchedulerMode::Locked) {
        return selected_profile_id_;
    }

    RadioProfileId next = selected_profile_id_;
    SchedulerSwitchReason applied_reason = reason;

    if (scheduler_mode_ == RadioSchedulerMode::Priority) {
        if (selected_profile_id_ == preferred_profile_id_) {
            next = next_enabled_profile(preferred_profile_id_);
            applied_reason = SchedulerSwitchReason::FallbackTimeout;
        } else {
            const RadioProfileId fallback_next = next_enabled_profile(selected_profile_id_);
            if (fallback_next == selected_profile_id_ || fallback_next == preferred_profile_id_) {
                next = preferred_profile_id_;
                applied_reason = SchedulerSwitchReason::PriorityReturn;
            } else {
                next = fallback_next;
                applied_reason = SchedulerSwitchReason::SchedulerAdvance;
            }
        }
    } else {
        next = next_enabled_profile(selected_profile_id_);
    }

    if (next != selected_profile_id_) {
        selected_profile_id_ = next;
        last_switch_reason_ = applied_reason;
        profile_switch_count_++;
    }
    return selected_profile_id_;
}

void RadioProfileManager::note_profile_applied(RadioProfileId applied_profile,
                                               ProfileApplyStatus status) {
    applied_profile_id_ = applied_profile;
    last_apply_status_ = status;
    profile_apply_count_++;
    if (status != ProfileApplyStatus::Applied) {
        profile_apply_failures_++;
    }
}

void RadioProfileManager::note_profile_apply_failure(ProfileApplyStatus status) {
    applied_profile_id_ = RadioProfileId::Unknown;
    last_apply_status_ = status;
    profile_apply_failures_++;
}

void RadioProfileManager::record_irq_wake() {
    last_wake_source_ = RuntimeWakeSource::IrqNotification;
    irq_wake_count_++;
}

void RadioProfileManager::record_fallback_wake() {
    last_wake_source_ = RuntimeWakeSource::FallbackPoll;
    fallback_wake_count_++;
}

} // namespace protocol_driver
