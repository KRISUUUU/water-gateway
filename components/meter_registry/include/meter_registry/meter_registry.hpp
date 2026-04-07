#pragma once

#include "common/result.hpp"
#include "radio_cc1101/radio_cc1101.hpp"
#include "wmbus_link/wmbus_link.hpp"
#include "wmbus_prios_rx/prios_decoder.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace meter_registry {

struct DetectedMeter {
    std::string key;
    uint16_t manufacturer_id = 0;
    uint32_t device_id = 0;
    int64_t first_seen_ms = 0;
    int64_t last_seen_ms = 0;
    uint32_t seen_count = 0;
    int8_t last_rssi_dbm = 0;
    uint8_t last_lqi = 0;
    bool last_crc_ok = false;
    uint32_t duplicate_count = 0;
    uint32_t crc_fail_count = 0;
    bool watched = false;
    bool watch_enabled = false;
    std::string alias;
    std::string note;
};

struct WatchlistEntry {
    std::string key;
    bool enabled = true;
    std::string alias;
    std::string note;
};

struct RecentTelegram {
    int64_t timestamp_ms = 0;
    std::string raw_hex;
    uint16_t frame_length = 0;
    std::string captured_hex;
    uint16_t captured_frame_length = 0;
    uint16_t payload_offset = 0;
    uint16_t payload_length = 0;
    uint8_t first_data_byte = 0;
    std::string canonical_hex;
    uint16_t canonical_frame_length = 0;
    bool decoded_ok = false;
    bool raw_frame_contract_valid = false;
    int8_t rssi_dbm = 0;
    uint8_t lqi = 0;
    bool crc_ok = false;
    bool radio_crc_available = false;
    bool duplicate = false;
    std::string meter_key;
    bool watched = false;
    // Protocol identity — populated for all protocols.
    // Defaults: "WMBUS_T" / "" for T-mode; "PRIOS_R3" or "PRIOS_R4" for PRIOS.
    std::string protocol_name = "WMBUS_T";
    std::string vendor;
};

enum class TelegramFilter : uint8_t {
    All = 0,
    WatchedOnly,
    UnknownOnly,
    DuplicatesOnly,
    CrcFailOnly,
};

class MeterRegistry {
  public:
    static constexpr std::size_t kMaxWatchlistSize = 50;

    static MeterRegistry& instance();

    common::Result<void> initialize();

    // Observes one processed frame and updates:
    // - detected meters model
    // - recent telegram list
    void observe_telegram(const wmbus_link::ValidatedTelegram& telegram, bool duplicate);
    // PRIOS decoded telegram: update detected meters and recent telegrams lists.
    void observe_prios_telegram(const wmbus_prios_rx::PriosDecodedTelegram& telegram);

    std::vector<DetectedMeter> detected_meters() const;
    std::vector<WatchlistEntry> watchlist() const;
    std::vector<RecentTelegram> recent_telegrams(TelegramFilter filter) const;

    common::Result<void> upsert_watchlist(const WatchlistEntry& entry);
    common::Result<void> remove_watchlist(const std::string& key);

  private:
    MeterRegistry() = default;

    static std::string derive_meter_key(const wmbus_link::ValidatedTelegram& telegram);
    static bool parse_watchlist_line(const std::string& line, WatchlistEntry& out);
    static std::string serialize_watchlist_line(const WatchlistEntry& entry);
    static std::string escape_field(const std::string& s);
    static std::string unescape_field(const std::string& s);

    common::Result<void> load_watchlist();
    common::Result<void> persist_watchlist() const;
    void apply_watchlist_to_detected_locked();

    bool initialized_ = false;
};

} // namespace meter_registry
