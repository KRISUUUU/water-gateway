#include "app_core/app_core.hpp"

extern "C" void app_main(void) {
    app_core::AppCore app;
    app.start();
}
