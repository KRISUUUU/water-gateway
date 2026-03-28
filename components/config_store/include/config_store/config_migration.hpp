#pragma once

#include "common/result.hpp"
#include "config_store/config_models.hpp"
#include <cstddef>
#include <cstdint>

namespace config_store {

// Migrates a config blob from its stored version to kCurrentConfigVersion.
// If the config is already at the current version, returns it unchanged.
// If the version is unknown/future, returns an error.
//
// Migration is a chain: v0→v1, v1→v2, etc. Each step applies defaults for
// new fields and removes obsolete fields.
common::Result<AppConfig> migrate_to_current(const AppConfig& old_config);

// Migrate a raw V1 NVS blob (old struct layout with admin_password_hash[98])
// to the current AppConfig (V2 with admin_password_hash[128]).
// Called from load_from_nvs() when stored blob size matches the V1 layout.
// Preserves all layout-compatible fields, including the existing password hash.
//
// Returns the blob size of the V1 struct (used by load_from_nvs for probing).
size_t config_v1_blob_size();
common::Result<AppConfig> migrate_v1_blob(const uint8_t* blob, size_t blob_size);

} // namespace config_store
