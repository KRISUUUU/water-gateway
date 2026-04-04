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

RadioProfileManager& RadioProfileManager::instance() {
    static RadioProfileManager s_instance;
    return s_instance;
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

void RadioProfileManager::configure(RadioSchedulerMode mode,
                                    RadioProfileMask   enabled_profiles) {
    scheduler_mode_   = mode;
    enabled_profiles_ = enabled_profiles != kRadioProfileMaskNone
                            ? enabled_profiles
                            : kRadioProfileMaskWMbusT868;
    active_profile_id_    = first_enabled_profile();
    last_switch_reason_   = SchedulerSwitchReason::Initial;
    profile_switch_count_ = 0;
    irq_wake_count_       = 0;
    fallback_wake_count_  = 0;
}

RadioProfileManager::Status RadioProfileManager::status() const {
    Status s;
    s.active_profile_id    = active_profile_id_;
    s.scheduler_mode       = scheduler_mode_;
    s.last_switch_reason   = last_switch_reason_;
    s.enabled_profiles     = enabled_profiles_;
    s.profile_switch_count = profile_switch_count_;
    s.irq_wake_count       = irq_wake_count_;
    s.fallback_wake_count  = fallback_wake_count_;
    return s;
}

RadioProfileId RadioProfileManager::active_profile_id() const {
    return active_profile_id_;
}

RadioProfileId RadioProfileManager::advance(SchedulerSwitchReason reason) {
    if (scheduler_mode_ == RadioSchedulerMode::Locked) {
        return active_profile_id_;
    }

    const RadioProfileId next = next_enabled_profile(active_profile_id_);
    if (next != active_profile_id_) {
        active_profile_id_  = next;
        last_switch_reason_ = reason;
        profile_switch_count_++;
    }
    return active_profile_id_;
}

void RadioProfileManager::record_irq_wake() {
    irq_wake_count_++;
}

void RadioProfileManager::record_fallback_wake() {
    fallback_wake_count_++;
}

} // namespace protocol_driver
