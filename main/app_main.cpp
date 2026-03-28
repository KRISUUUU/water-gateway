#include "app_core/app_core.hpp"

extern "C" void app_main(void) {
    // Keep the application root alive for the full firmware lifetime.
    static app_core::AppCore core;
    core.start();
}
