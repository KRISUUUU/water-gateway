#include "config_store/config_migration.hpp"

namespace config_store {

common::Result<AppConfig> migrate_to_current(const AppConfig& input) {
    AppConfig migrated = input;

    if (migrated.version == 0) {
        migrated.version = kCurrentConfigVersion;
    }

    if (migrated.version != kCurrentConfigVersion) {
        return common::Result<AppConfig>(common::ErrorCode::NotSupported);
    }

    return common::Result<AppConfig>(migrated);
}

}  // namespace config_store
