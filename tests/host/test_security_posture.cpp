#include "common/security_posture.hpp"

#include <cassert>

int main() {
    const auto sec = common::build_security_posture();

    // Host tests should compile and expose deterministic bool posture fields.
    assert((sec.secure_boot_enabled == true || sec.secure_boot_enabled == false));
    assert((sec.flash_encryption_enabled == true || sec.flash_encryption_enabled == false));
    assert((sec.nvs_encryption_enabled == true || sec.nvs_encryption_enabled == false));
    assert((sec.anti_rollback_enabled == true || sec.anti_rollback_enabled == false));
    assert((sec.ota_rollback_enabled == true || sec.ota_rollback_enabled == false));

    const bool expected_ready = sec.secure_boot_enabled && sec.flash_encryption_enabled &&
                                sec.nvs_encryption_enabled && sec.anti_rollback_enabled &&
                                sec.ota_rollback_enabled;
    assert(common::build_is_hardened_for_production() == expected_ready);

    return 0;
}
