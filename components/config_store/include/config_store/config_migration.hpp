#pragma once

#include "common/result.hpp"
#include "config_store/config_models.hpp"

namespace config_store {

common::Result<AppConfig> migrate_to_current(const AppConfig& input);

}  // namespace config_store
