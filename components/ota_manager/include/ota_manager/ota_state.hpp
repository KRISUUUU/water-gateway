#pragma once

#include <cstdint>

namespace ota_manager {

enum class OtaState : uint8_t {
    Idle = 0,
    InProgress,
    Validating,
    Rebooting,
    Failed,
};

struct OtaStatus {
    OtaState state = OtaState::Idle;
    uint8_t progress_pct = 0;
    char message[128] = {};
    char current_version[16] = {};
    bool boot_pending_verify = false;
    bool boot_marked_valid = false;
    uint32_t boot_mark_attempts = 0;
    uint32_t boot_mark_failures = 0;
    int32_t last_boot_mark_error = 0;
    char boot_validation_note[64] = {};
};

inline const char* ota_state_to_string(OtaState state) {
    switch (state) {
    case OtaState::Idle:
        return "idle";
    case OtaState::InProgress:
        return "in_progress";
    case OtaState::Validating:
        return "validating";
    case OtaState::Rebooting:
        return "rebooting";
    case OtaState::Failed:
        return "failed";
    default:
        return "unknown";
    }
}

} // namespace ota_manager
