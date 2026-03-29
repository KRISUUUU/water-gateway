#pragma once

namespace common {

struct BuildSecurityPosture {
    bool secure_boot_enabled{false};
    bool flash_encryption_enabled{false};
    bool nvs_encryption_enabled{false};
    bool anti_rollback_enabled{false};
    bool ota_rollback_enabled{false};
};

inline constexpr bool kSecureBootEnabled =
#if defined(CONFIG_SECURE_BOOT) && CONFIG_SECURE_BOOT
    true;
#else
    false;
#endif

inline constexpr bool kFlashEncryptionEnabled =
#if defined(CONFIG_SECURE_FLASH_ENC_ENABLED) && CONFIG_SECURE_FLASH_ENC_ENABLED
    true;
#elif defined(CONFIG_FLASH_ENCRYPTION_ENABLED) && CONFIG_FLASH_ENCRYPTION_ENABLED
    true;
#else
    false;
#endif

inline constexpr bool kNvsEncryptionEnabled =
#if defined(CONFIG_NVS_ENCRYPTION) && CONFIG_NVS_ENCRYPTION
    true;
#else
    false;
#endif

inline constexpr bool kAntiRollbackEnabled =
#if defined(CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK) && CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
    true;
#else
    false;
#endif

inline constexpr bool kOtaRollbackEnabled =
#if defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) && CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    true;
#else
    false;
#endif

inline constexpr BuildSecurityPosture build_security_posture() {
    return BuildSecurityPosture{kSecureBootEnabled, kFlashEncryptionEnabled, kNvsEncryptionEnabled,
                                kAntiRollbackEnabled, kOtaRollbackEnabled};
}

inline constexpr bool build_is_hardened_for_production() {
    return kSecureBootEnabled && kFlashEncryptionEnabled && kNvsEncryptionEnabled &&
           kAntiRollbackEnabled && kOtaRollbackEnabled;
}

} // namespace common
