#pragma once

#include <string>

namespace ota_manager {

enum class OtaState {
    Idle = 0,
    Validating,
    Downloading,
    Writing,
    PendingReboot,
    Success,
    Failed,
    RollbackPending
};

struct OtaStatus {
    OtaState state{OtaState::Idle};
    std::string message{};
    int progress_percent{0};
};

}  // namespace ota_manager
