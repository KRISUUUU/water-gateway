#pragma once

#include "common/result.hpp"
#include "config_store/config_models.hpp"

namespace config_store {

// Migrates a config blob from its stored version to kCurrentConfigVersion.
// If the config is already at the current version, returns it unchanged.
// If the version is unknown/future, returns an error.
//
// Migration is a chain: v0→v1, v1→v2, etc. Each step applies defaults for
// new fields and removes obsolete fields.
common::Result<AppConfig> migrate_to_current(const AppConfig& old_config);

} // namespace config_store
