#pragma once

#include "config_store/config_models.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace config_store {

enum class ValidationSeverity : uint8_t {
    Error = 0,   // Blocks saving
    Warning = 1, // Allowed but suspicious
};

struct ValidationIssue {
    ValidationSeverity severity;
    std::string field;
    std::string message;
};

struct ValidationResult {
    bool valid;
    std::vector<ValidationIssue> issues;

    static ValidationResult success() {
        return {true, {}};
    }

    void add_error(const std::string& field, const std::string& message) {
        issues.push_back({ValidationSeverity::Error, field, message});
        valid = false;
    }

    void add_warning(const std::string& field, const std::string& message) {
        issues.push_back({ValidationSeverity::Warning, field, message});
    }
};

// Validates an AppConfig. Returns a ValidationResult with all issues found.
// A config with any Error-severity issues is not valid for persistence.
ValidationResult validate_config(const AppConfig& config);

// Validates that a hostname contains only allowed characters: a-z, 0-9, hyphens.
// Must not start or end with a hyphen.
bool is_valid_hostname(const char* hostname);

} // namespace config_store
