#include "diagnostics_service/diagnostics_service.hpp"

#include <cassert>
#include <string>

int main() {
    auto snap_res = diagnostics_service::DiagnosticsService::instance().snapshot();
    assert(!snap_res.is_error());
    const auto snap = snap_res.value();

    // Host build has no NTP sync and uses monotonic fallback visibility fields.
    assert(!snap.ntp.synchronized);
    assert(snap.now_epoch_ms == 0);
    assert(snap.timestamp_uses_monotonic_fallback);

    const std::string json = diagnostics_service::DiagnosticsService::to_json(snap);
    assert(json.find("\"time\"") != std::string::npos);
    assert(json.find("\"timestamp_source\":\"monotonic\"") != std::string::npos);
    assert(json.find("\"reset_reason\"") != std::string::npos);
    assert(json.find("\"radio_stack_hwm_words\"") != std::string::npos);
    return 0;
}
