#pragma once

#include "config_store/config_models.hpp"

#include <string>

struct cJSON;

namespace api_handlers::detail {

std::string config_to_json_redacted(const config_store::AppConfig& config);
void apply_config_json(const cJSON* root, config_store::AppConfig& cfg);

} // namespace api_handlers::detail
