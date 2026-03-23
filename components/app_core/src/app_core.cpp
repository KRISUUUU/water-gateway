#include "app_core/app_core.hpp"

#include "config_store/config_store.hpp"
#include "event_bus/event_bus.hpp"

namespace app_core {

void AppCore::start() {
    initialize_core_services();
    determine_start_mode();
    start_runtime();
}

void AppCore::initialize_core_services() {
    event_bus::EventBus::instance().initialize();
    config_store::ConfigStore::instance().initialize();
}

void AppCore::determine_start_mode() {
    // Placeholder:
    // In later steps this should decide between provisioning mode,
    // normal mode, and possibly maintenance/recovery mode based on
    // config state, health info, and OTA boot status.
}

void AppCore::start_runtime() {
    // Placeholder:
    // In later steps this should start the orchestrated runtime flow
    // for services, tasks, radio pipeline, connectivity, and UI.
}

}  // namespace app_core
