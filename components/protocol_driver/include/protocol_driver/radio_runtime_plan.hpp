#pragma once

#include "protocol_driver/protocol_ids.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace protocol_driver {

static constexpr size_t kSupportedRadioInstanceCount = 2;

struct RadioInstanceRuntimeConfig {
    RadioInstanceId    radio_instance  = kRadioInstancePrimary;
    bool               present         = false;
    bool               enabled         = false;
    RadioSchedulerMode scheduler_mode  = RadioSchedulerMode::Locked;
    RadioProfileMask   enabled_profiles = kRadioProfileMaskNone;
};

struct RadioRuntimePlan {
    std::array<RadioInstanceRuntimeConfig, kSupportedRadioInstanceCount> instances{};
    uint8_t active_radio_count = 0;
    bool single_radio_mode = true;

    const RadioInstanceRuntimeConfig* instance(RadioInstanceId radio_instance) const {
        for (const auto& entry : instances) {
            if (entry.radio_instance == radio_instance) {
                return &entry;
            }
        }
        return nullptr;
    }

    static RadioRuntimePlan single_radio(RadioSchedulerMode scheduler_mode,
                                         RadioProfileMask enabled_profiles) {
        RadioRuntimePlan plan{};
        plan.instances[0] = {
            kRadioInstancePrimary,
            true,
            true,
            scheduler_mode,
            enabled_profiles != kRadioProfileMaskNone
                ? enabled_profiles
                : kRadioProfileMaskWMbusT868,
        };
        plan.instances[1] = {
            kRadioInstanceSecondary,
            false,
            false,
            RadioSchedulerMode::Locked,
            kRadioProfileMaskNone,
        };
        plan.active_radio_count = 1;
        plan.single_radio_mode = true;
        return plan;
    }
};

} // namespace protocol_driver
