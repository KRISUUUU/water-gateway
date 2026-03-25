#include "event_bus/event_bus.hpp"
#include "ota_manager/ota_manager.hpp"

#include <cassert>
#include <cstdint>

int main() {
    auto bus_init = event_bus::EventBus::instance().initialize();
    assert(!bus_init.is_error());

    auto& ota = ota_manager::OtaManager::instance();
    auto init = ota.initialize();
    assert(!init.is_error());

    constexpr uint8_t fw_stub[8] = {0xE9, 0x01, 0x02, 0x03, 0x10, 0x20, 0x30, 0x40};
    auto begin = ota.begin_upload(sizeof(fw_stub));
    assert(!begin.is_error());
    auto write = ota.write_chunk(fw_stub, sizeof(fw_stub));
    assert(!write.is_error());
    auto fin = ota.finalize_upload();
    assert(!fin.is_error());

    const auto st = ota.status();
    assert(st.progress_pct == 100);
    assert(st.state == ota_manager::OtaState::Rebooting);
    return 0;
}
