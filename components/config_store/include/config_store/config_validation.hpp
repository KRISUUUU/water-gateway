#pragma once

#include <string>
#include <vector>

#include "config_store/config_models.hpp"

namespace config_store {

struct ValidationIssue {
    std::string field;
    std::string message;
};

struct ValidationResult {
    bool valid{true};
    std::vector<ValidationIssue> issues{};
};

ValidationResult validate_config(const AppConfig& config);

}  // namespace config_store
