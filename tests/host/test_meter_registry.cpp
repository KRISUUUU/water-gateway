#include "meter_registry/meter_registry.hpp"
#include "wmbus_link/wmbus_link.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {

size_t hex_to_bytes(const char* hex, uint8_t* out, size_t out_size) {
    if (!hex || !out) {
        return 0;
    }

    const auto hex_nibble = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') {
            return static_cast<uint8_t>(c - '0');
        }
        if (c >= 'A' && c <= 'F') {
            return static_cast<uint8_t>(c - 'A' + 10);
        }
        if (c >= 'a' && c <= 'f') {
            return static_cast<uint8_t>(c - 'a' + 10);
        }
        return 0;
    };

    const size_t hex_len = std::strlen(hex);
    const size_t bytes = std::min(out_size, hex_len / 2U);
    for (size_t i = 0; i < bytes; ++i) {
        out[i] = static_cast<uint8_t>((hex_nibble(hex[i * 2U]) << 4U) |
                                      hex_nibble(hex[i * 2U + 1U]));
    }
    return bytes;
}

wmbus_link::ValidatedTelegram make_telegram(const char* canonical_hex, int64_t ts_ms, bool crc_ok,
                                            bool reliable_identity, int8_t rssi = -70,
                                            uint8_t lqi = 40) {
    wmbus_link::ValidatedTelegram telegram{};
    uint8_t canonical[300]{};
    const size_t canonical_len = hex_to_bytes(canonical_hex, canonical, sizeof(canonical));
    for (size_t i = 0; i < canonical_len; ++i) {
        telegram.link.canonical_bytes[i] = canonical[i];
    }
    telegram.link.metadata.timestamp_ms = ts_ms;
    telegram.link.metadata.crc_ok = crc_ok;
    telegram.link.metadata.rssi_dbm = rssi;
    telegram.link.metadata.lqi = lqi;
    telegram.link.metadata.canonical_length = static_cast<uint16_t>(canonical_len);
    telegram.link.metadata.encoded_length = static_cast<uint16_t>(canonical_len);
    telegram.link.metadata.exact_encoded_bytes_required = static_cast<uint16_t>(canonical_len);
    telegram.exact_frame.metadata.timestamp_ms = ts_ms;
    telegram.exact_frame.metadata.capture_elapsed_ms = 3;
    telegram.exact_frame.metadata.captured_frame_length = static_cast<uint16_t>(canonical_len);
    telegram.exact_frame.metadata.first_data_byte = canonical_len > 0U ? canonical[0] : 0U;
    telegram.exact_frame.metadata.exact_frame_contract_valid = true;
    telegram.exact_frame.encoded_length = static_cast<uint16_t>(canonical_len);
    telegram.exact_frame.exact_encoded_bytes_required = static_cast<uint16_t>(canonical_len);
    for (size_t i = 0; i < canonical_len; ++i) {
        telegram.exact_frame.encoded_bytes[i] = canonical[i];
    }

    telegram.link.reliable_identity = reliable_identity;
    if (reliable_identity && canonical_len >= 8U) {
        telegram.link.manufacturer_id =
            static_cast<uint16_t>((static_cast<uint16_t>(canonical[3]) << 8U) | canonical[2]);
        telegram.link.device_id = (static_cast<uint32_t>(canonical[7]) << 24U) |
                                  (static_cast<uint32_t>(canonical[6]) << 16U) |
                                  (static_cast<uint32_t>(canonical[5]) << 8U) |
                                  static_cast<uint32_t>(canonical[4]);
    }
    return telegram;
}

} // namespace

int main() {
    auto& registry = meter_registry::MeterRegistry::instance();
    auto init = registry.initialize();
    assert(!init.is_error());

    auto telegram1 = make_telegram("2C4493157856341201078C20", 1000, true, false, -65, 45);
    registry.observe_telegram(telegram1, false);

    auto meters = registry.detected_meters();
    assert(!meters.empty());
    assert(meters[0].seen_count >= 1);
    assert(!meters[0].key.empty());
    assert(meters[0].key == "sig:2C4493157856341201078C20");
    assert(meters[0].manufacturer_id == 0);
    assert(meters[0].device_id == 0);

    meter_registry::WatchlistEntry wl{};
    wl.key = meters[0].key;
    wl.alias = "Kitchen meter";
    wl.note = "priority";
    wl.enabled = true;
    auto save = registry.upsert_watchlist(wl);
    assert(!save.is_error());

    auto watched_telegram = make_telegram("2C4493157856341201078C20AA", 2000, false, false, -72, 30);
    registry.observe_telegram(watched_telegram, true);

    auto watched = registry.recent_telegrams(meter_registry::TelegramFilter::WatchedOnly);
    assert(!watched.empty());
    assert(watched.front().watched);
    assert(watched.front().raw_frame_contract_valid == true);
    assert(watched.front().decoded_ok == true);
    assert(watched.front().canonical_hex == watched.front().raw_hex);
    assert(watched.front().canonical_frame_length == watched.front().frame_length);

    auto dup = registry.recent_telegrams(meter_registry::TelegramFilter::DuplicatesOnly);
    assert(!dup.empty());
    assert(dup.front().duplicate);

    auto crc_fail = registry.recent_telegrams(meter_registry::TelegramFilter::CrcFailOnly);
    assert(!crc_fail.empty());
    assert(!crc_fail.front().crc_ok);

    auto rm = registry.remove_watchlist(wl.key);
    assert(!rm.is_error());

    for (size_t i = 0; i < meter_registry::MeterRegistry::kMaxWatchlistSize; ++i) {
        meter_registry::WatchlistEntry item{};
        char key[32]{};
        std::snprintf(key, sizeof(key), "sig:test-%02zu", i);
        item.key = key;
        item.alias = "meter";
        auto save_item = registry.upsert_watchlist(item);
        assert(!save_item.is_error());
    }

    meter_registry::WatchlistEntry overflow{};
    overflow.key = "sig:overflow";
    auto overflow_result = registry.upsert_watchlist(overflow);
    assert(overflow_result.is_error());
    assert(overflow_result.error() == common::ErrorCode::BufferFull);

    for (size_t i = 0; i < 220; ++i) {
        char hex[64]{};
        std::snprintf(hex, sizeof(hex), "2C44AA55%08X01078C20", static_cast<unsigned int>(i));
        registry.observe_telegram(make_telegram(hex, 10000 + static_cast<int64_t>(i), true, false),
                                  false);
    }

    meters = registry.detected_meters();
    assert(meters.size() == 100);
    bool found_oldest = false;
    bool found_newest = false;
    for (const auto& meter : meters) {
        if (meter.key == "sig:2C44AA550000000001078C20") {
            found_oldest = true;
        }
        if (meter.key == "sig:2C44AA55000000DB01078C20") {
            found_newest = true;
        }
    }
    assert(!found_oldest);
    assert(found_newest);

    auto decoded = make_telegram("0B44840D9048460601070000", 20000, true, true);
    decoded.exact_frame.encoded_length = 2;
    decoded.exact_frame.exact_encoded_bytes_required = 2;
    decoded.exact_frame.metadata.captured_frame_length = 2;
    decoded.exact_frame.metadata.first_data_byte = 0x0E;
    decoded.exact_frame.encoded_bytes[0] = 0x0E;
    decoded.exact_frame.encoded_bytes[1] = 0x16;
    decoded.link.metadata.radio_crc_available = false;
    registry.observe_telegram(decoded, false);
    meters = registry.detected_meters();
    bool found_decoded = false;
    for (const auto& meter : meters) {
        if (meter.key == "mfg:0D84-id:06464890") {
            found_decoded = true;
            assert(meter.manufacturer_id == 0x0D84);
            assert(meter.device_id == 0x06464890);
        }
    }
    assert(found_decoded);
    auto all = registry.recent_telegrams(meter_registry::TelegramFilter::All);
    assert(!all.empty());
    assert(all.front().decoded_ok == true);
    assert(all.front().raw_frame_contract_valid == true);
    assert(all.front().captured_frame_length == 2);
    assert(all.front().canonical_frame_length == decoded.link.metadata.canonical_length);
    assert(all.front().first_data_byte == 0x0E);
    assert(all.front().radio_crc_available == false);
    return 0;
}
