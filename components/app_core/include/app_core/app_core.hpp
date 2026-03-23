#pragma once

namespace app_core {

class AppCore {
public:
    AppCore() = default;
    ~AppCore() = default;

    void start();

private:
    void initialize_core_services();
    void determine_start_mode();
    void start_runtime();
};

}  // namespace app_core
