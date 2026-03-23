#pragma once

#include "common/result.hpp"
#include "config_store/config_models.hpp"
#include "config_store/config_validation.hpp"

namespace config_store {

class ConfigStore {
public:
    static ConfigStore& instance();

    common::Result<void> initialize();
    common::Result<AppConfig> load() const;
    common::Result<void> save(const AppConfig& config);
    common::Result<void> reset_to_defaults();

    [[nodiscard]] bool is_initialized() const;
    [[nodiscard]] bool has_valid_config() const;

private:
    ConfigStore() = default;

    bool initialized_{false};
    AppConfig current_config_{};
    bool has_valid_config_{false};
};

}  // namespace config_store
