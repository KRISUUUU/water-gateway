#include "event_bus/event_bus.hpp"
#include "ota_manager/ota_manager.hpp"

#include <cassert>
#include <cstdint>

int main() {
    auto bus_init = event_bus::EventBus::instance().initialize();
    assert(!bus_init.is_error());

    auto& ota = ota_manager::OtaManager::instance();
    auto begin_without_init = ota.begin_upload(16);
    assert(begin_without_init.is_error());
    assert(begin_without_init.error() == common::ErrorCode::NotInitialized);

    auto init = ota.initialize();
    assert(!init.is_error());

    auto write_before_begin = ota.write_chunk(reinterpret_cast<const uint8_t*>("x"), 1);
    assert(write_before_begin.is_error());
    assert(write_before_begin.error() == common::ErrorCode::InvalidArgument);

    auto finalize_before_begin = ota.finalize_upload();
    assert(finalize_before_begin.is_error());
    assert(finalize_before_begin.error() == common::ErrorCode::InvalidArgument);

    auto url_null = ota.begin_url_ota(nullptr);
    assert(url_null.is_error());
    assert(url_null.error() == common::ErrorCode::InvalidArgument);

    auto url_empty = ota.begin_url_ota("");
    assert(url_empty.is_error());
    assert(url_empty.error() == common::ErrorCode::InvalidArgument);

    auto url_async_null = ota.begin_url_ota_async(nullptr);
    assert(url_async_null.is_error());
    assert(url_async_null.error() == common::ErrorCode::InvalidArgument);

    auto url_async_empty = ota.begin_url_ota_async("");
    assert(url_async_empty.is_error());
    assert(url_async_empty.error() == common::ErrorCode::InvalidArgument);

    auto boot_valid = ota.mark_boot_valid();
    assert(!boot_valid.is_error());

    constexpr uint8_t fw_stub[8] = {0xE9, 0x01, 0x02, 0x03, 0x10, 0x20, 0x30, 0x40};
    auto begin = ota.begin_upload(sizeof(fw_stub));
    assert(!begin.is_error());
    auto begin_again = ota.begin_upload(sizeof(fw_stub));
    assert(begin_again.is_error());
    assert(begin_again.error() == common::ErrorCode::OtaAlreadyInProgress);

    auto write = ota.write_chunk(fw_stub, sizeof(fw_stub));
    assert(!write.is_error());
    auto abort = ota.abort_upload();
    assert(!abort.is_error());
    assert(ota.status().state == ota_manager::OtaState::Idle);

    auto finalize_after_abort = ota.finalize_upload();
    assert(finalize_after_abort.is_error());
    assert(finalize_after_abort.error() == common::ErrorCode::InvalidArgument);

    begin = ota.begin_upload(sizeof(fw_stub));
    assert(!begin.is_error());
    write = ota.write_chunk(fw_stub, sizeof(fw_stub));
    assert(!write.is_error());
    auto fin = ota.finalize_upload();
    assert(!fin.is_error());

    auto write_after_finalize = ota.write_chunk(fw_stub, sizeof(fw_stub));
    assert(write_after_finalize.is_error());
    assert(write_after_finalize.error() == common::ErrorCode::InvalidArgument);

    const auto st = ota.status();
    assert(st.progress_pct == 100);
    assert(st.state == ota_manager::OtaState::Rebooting);
    assert(st.boot_mark_attempts >= 1);
    assert(st.boot_mark_failures == 0);
    assert(st.boot_marked_valid);

    auto url_async = ota.begin_url_ota_async("https://example.com/fw.bin");
    assert(!url_async.is_error());
    assert(ota.status().state == ota_manager::OtaState::Rebooting);
    return 0;
}
