#include "meter_registry/meter_registry.hpp"
#include "wmbus_minimal_pipeline/wmbus_pipeline.hpp"

#include <cassert>

static wmbus_minimal_pipeline::WmbusFrame make_frame(const char* hex, int64_t ts_ms, bool crc_ok,
                                                     int8_t rssi = -70, uint8_t lqi = 40) {
    wmbus_minimal_pipeline::WmbusFrame f{};
    uint8_t bytes[300] = {};
    const size_t n = wmbus_minimal_pipeline::WmbusPipeline::hex_to_bytes(hex, bytes, sizeof(bytes));
    f.raw_bytes.assign(bytes, bytes + n);
    f.metadata.timestamp_ms = ts_ms;
    f.metadata.crc_ok = crc_ok;
    f.metadata.rssi_dbm = rssi;
    f.metadata.lqi = lqi;
    f.metadata.frame_length = static_cast<uint16_t>(f.raw_bytes.size());
    return f;
}

int main() {
    auto& registry = meter_registry::MeterRegistry::instance();
    auto init = registry.initialize();
    assert(!init.is_error());

    // Has manufacturer/device bytes in canonical positions (simplified frame)
    auto frame1 = make_frame("2C4493157856341201078C20", 1000, true, -65, 45);
    registry.observe_frame(frame1, false);

    auto meters = registry.detected_meters();
    assert(!meters.empty());
    assert(meters[0].seen_count >= 1);
    assert(!meters[0].key.empty());
    assert(meters[0].key == "mfg:1593-id:12345678");

    meter_registry::WatchlistEntry wl{};
    wl.key = meters[0].key;
    wl.alias = "Kitchen meter";
    wl.note = "priority";
    wl.enabled = true;
    auto save = registry.upsert_watchlist(wl);
    assert(!save.is_error());

    auto watched_tele = make_frame("2C4493157856341201078C21", 2000, false, -72, 30);
    registry.observe_frame(watched_tele, true);

    auto watched = registry.recent_telegrams(meter_registry::TelegramFilter::WatchedOnly);
    assert(!watched.empty());
    assert(watched.front().watched);

    auto dup = registry.recent_telegrams(meter_registry::TelegramFilter::DuplicatesOnly);
    assert(!dup.empty());
    assert(dup.front().duplicate);

    auto crc_fail = registry.recent_telegrams(meter_registry::TelegramFilter::CrcFailOnly);
    assert(!crc_fail.empty());
    assert(!crc_fail.front().crc_ok);

    auto rm = registry.remove_watchlist(wl.key);
    assert(!rm.is_error());
    return 0;
}
