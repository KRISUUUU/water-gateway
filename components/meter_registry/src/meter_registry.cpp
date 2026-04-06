#include "meter_registry/meter_registry.hpp"

#include "storage_service/storage_service.hpp"

#ifndef HOST_TEST_BUILD
#include "esp_log.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace meter_registry {

namespace {

#ifndef HOST_TEST_BUILD
static const char* TAG = "meter_registry";
#endif

static constexpr const char* kWatchlistFile = "watchlist.db";
static constexpr size_t kMaxRecentTelegrams = 50;
static constexpr size_t kMaxDetectedMeters = 100;

struct RegistryState {
    std::vector<DetectedMeter> detected;
    std::vector<RecentTelegram> recent;
    std::vector<WatchlistEntry> watchlist;
    std::mutex mutex;
};

RegistryState& state() {
    static RegistryState s;
    return s;
}

int watchlist_index_by_key(const std::vector<WatchlistEntry>& list, const std::string& key) {
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].key == key) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int detected_index_by_key(const std::vector<DetectedMeter>& list, const std::string& key) {
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].key == key) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

MeterRegistry& MeterRegistry::instance() {
    static MeterRegistry registry;
    return registry;
}

common::Result<void> MeterRegistry::initialize() {
    if (initialized_) {
        return common::Result<void>::error(common::ErrorCode::AlreadyInitialized);
    }
    initialized_ = true;

    auto load_result = load_watchlist();
    if (load_result.is_error()) {
#ifndef HOST_TEST_BUILD
        ESP_LOGW(TAG, "Watchlist load failed, continuing (%d)",
                 static_cast<int>(load_result.error()));
#endif
    }
    return common::Result<void>::ok();
}

void MeterRegistry::observe_telegram(const wmbus_link::ValidatedTelegram& telegram,
                                     bool duplicate) {
    if (!initialized_) {
        return;
    }
    RegistryState& s = state();
    const std::string key = derive_meter_key(telegram);

    std::lock_guard<std::mutex> lock(s.mutex);
    int idx = detected_index_by_key(s.detected, key);
    if (idx < 0) {
        if (s.detected.size() >= kMaxDetectedMeters) {
            auto oldest = std::min_element(
                s.detected.begin(), s.detected.end(),
                [](const DetectedMeter& a, const DetectedMeter& b) { return a.last_seen_ms < b.last_seen_ms; });
            if (oldest != s.detected.end()) {
                s.detected.erase(oldest);
            }
        }
        DetectedMeter m{};
        m.key = key;
        if (telegram.has_reliable_identity()) {
            m.manufacturer_id = telegram.manufacturer_id();
            m.device_id = telegram.device_id();
        }
        m.first_seen_ms = telegram.link.metadata.timestamp_ms;
        m.last_seen_ms = telegram.link.metadata.timestamp_ms;
        m.seen_count = 1;
        m.last_rssi_dbm = telegram.link.metadata.rssi_dbm;
        m.last_lqi = telegram.link.metadata.lqi;
        m.last_crc_ok = telegram.link.metadata.crc_ok;
        s.detected.push_back(std::move(m));
        if (s.detected.size() > 100) {
            auto oldest = std::min_element(
                s.detected.begin(), s.detected.end(),
                [](const DetectedMeter& a, const DetectedMeter& b) {
                    return a.last_seen_ms < b.last_seen_ms;
                });
            if (oldest != s.detected.end()) {
                s.detected.erase(oldest);
            }
        }
        idx = detected_index_by_key(s.detected, key);
        if (idx < 0) {
            return;
        }
    } else {
        DetectedMeter& m = s.detected[static_cast<size_t>(idx)];
        if (telegram.has_reliable_identity()) {
            m.manufacturer_id = telegram.manufacturer_id();
            m.device_id = telegram.device_id();
        }
        m.last_seen_ms = telegram.link.metadata.timestamp_ms;
        m.seen_count++;
        m.last_rssi_dbm = telegram.link.metadata.rssi_dbm;
        m.last_lqi = telegram.link.metadata.lqi;
        m.last_crc_ok = telegram.link.metadata.crc_ok;
    }

    DetectedMeter& meter = s.detected[static_cast<size_t>(idx)];
    if (!telegram.link.metadata.crc_ok) {
        meter.crc_fail_count++;
    }
    if (duplicate) {
        meter.duplicate_count++;
    }

    const int wl_idx = watchlist_index_by_key(s.watchlist, key);
    const bool watched = wl_idx >= 0 && s.watchlist[static_cast<size_t>(wl_idx)].enabled;
    meter.watched = wl_idx >= 0;
    meter.watch_enabled = watched;
    if (wl_idx >= 0) {
        meter.alias = s.watchlist[static_cast<size_t>(wl_idx)].alias;
        meter.note = s.watchlist[static_cast<size_t>(wl_idx)].note;
    }

    RecentTelegram t{};
    t.timestamp_ms = telegram.link.metadata.timestamp_ms;
    t.raw_hex = telegram.canonical_hex();
    t.frame_length = telegram.link.metadata.canonical_length;
    t.captured_hex = telegram.captured_hex();
    t.captured_frame_length = telegram.exact_frame.metadata.captured_frame_length;
    t.payload_offset = 0;
    t.payload_length = telegram.exact_frame.encoded_length;
    t.first_data_byte = telegram.exact_frame.metadata.first_data_byte;
    t.canonical_hex = telegram.canonical_hex();
    t.canonical_frame_length = telegram.link.metadata.canonical_length;
    t.decoded_ok = true;
    t.raw_frame_contract_valid = telegram.exact_frame.metadata.exact_frame_contract_valid;
    t.rssi_dbm = telegram.link.metadata.rssi_dbm;
    t.lqi = telegram.link.metadata.lqi;
    t.crc_ok = telegram.link.metadata.crc_ok;
    t.radio_crc_available = telegram.link.metadata.radio_crc_available;
    t.duplicate = duplicate;
    t.meter_key = key;
    t.watched = watched;
    t.protocol_name = "WMBUS_T";
    s.recent.push_back(std::move(t));
    if (s.recent.size() > kMaxRecentTelegrams) {
        s.recent.erase(s.recent.begin(),
                       s.recent.begin() + (s.recent.size() - kMaxRecentTelegrams));
    }
}

void MeterRegistry::observe_prios_telegram(
    const wmbus_prios_rx::PriosDecodedTelegram& telegram) {
    if (!initialized_ || !telegram.valid) {
        return;
    }
    RegistryState& s = state();
    const std::string key = telegram.meter_key;

    std::lock_guard<std::mutex> lock(s.mutex);

    // Detected meters
    int idx = detected_index_by_key(s.detected, key);
    if (idx < 0) {
        if (s.detected.size() >= kMaxDetectedMeters) {
            auto oldest = std::min_element(
                s.detected.begin(), s.detected.end(),
                [](const DetectedMeter& a, const DetectedMeter& b) {
                    return a.last_seen_ms < b.last_seen_ms;
                });
            if (oldest != s.detected.end()) {
                s.detected.erase(oldest);
            }
        }
        DetectedMeter m{};
        m.key             = key;
        m.manufacturer_id = telegram.manufacturer_id;
        m.device_id       = telegram.meter_id;
        m.first_seen_ms   = telegram.timestamp_ms;
        m.last_seen_ms    = telegram.timestamp_ms;
        m.seen_count      = 1;
        m.last_rssi_dbm   = telegram.rssi_dbm;
        m.last_lqi        = telegram.lqi;
        m.last_crc_ok     = true;
        s.detected.push_back(std::move(m));
        idx = detected_index_by_key(s.detected, key);
        if (idx < 0) {
            return;
        }
    } else {
        DetectedMeter& m = s.detected[static_cast<size_t>(idx)];
        m.last_seen_ms  = telegram.timestamp_ms;
        m.seen_count++;
        m.last_rssi_dbm = telegram.rssi_dbm;
        m.last_lqi      = telegram.lqi;
        m.last_crc_ok   = true;
    }

    DetectedMeter& meter = s.detected[static_cast<size_t>(idx)];
    const int wl_idx = watchlist_index_by_key(s.watchlist, key);
    const bool watched = wl_idx >= 0 && s.watchlist[static_cast<size_t>(wl_idx)].enabled;
    meter.watched      = wl_idx >= 0;
    meter.watch_enabled = watched;
    if (wl_idx >= 0) {
        meter.alias = s.watchlist[static_cast<size_t>(wl_idx)].alias;
        meter.note  = s.watchlist[static_cast<size_t>(wl_idx)].note;
    }

    // Recent telegrams
    RecentTelegram t{};
    t.timestamp_ms          = telegram.timestamp_ms;
    t.raw_hex               = telegram.display_prefix_hex;
    t.frame_length          = telegram.captured_length;
    t.captured_hex          = telegram.display_prefix_hex;
    t.captured_frame_length = telegram.captured_length;
    t.canonical_hex         = telegram.meter_key;
    t.canonical_frame_length = telegram.captured_length;
    t.decoded_ok            = false;
    t.raw_frame_contract_valid = false;
    t.rssi_dbm              = telegram.rssi_dbm;
    t.lqi                   = telegram.lqi;
    t.crc_ok                = true;
    t.radio_crc_available   = false;
    t.duplicate             = false;
    t.meter_key             = key;
    t.watched               = watched;
    t.protocol_name         = wmbus_prios_rx::PriosDecodedTelegram::kProtocolName;
    t.vendor                = telegram.manufacturer;

    s.recent.push_back(std::move(t));
    if (s.recent.size() > kMaxRecentTelegrams) {
        s.recent.erase(s.recent.begin(),
                       s.recent.begin() + (s.recent.size() - kMaxRecentTelegrams));
    }
}

std::vector<DetectedMeter> MeterRegistry::detected_meters() const {
    RegistryState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    std::vector<DetectedMeter> out = s.detected;
    std::sort(out.begin(), out.end(), [](const DetectedMeter& a, const DetectedMeter& b) {
        return a.last_seen_ms > b.last_seen_ms;
    });
    return out;
}

std::vector<WatchlistEntry> MeterRegistry::watchlist() const {
    RegistryState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.watchlist;
}

std::vector<RecentTelegram> MeterRegistry::recent_telegrams(TelegramFilter filter) const {
    RegistryState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    std::vector<RecentTelegram> out;
    out.reserve(s.recent.size());
    for (auto it = s.recent.rbegin(); it != s.recent.rend(); ++it) {
        const RecentTelegram& t = *it;
        bool include = false;
        switch (filter) {
        case TelegramFilter::All:
            include = true;
            break;
        case TelegramFilter::WatchedOnly:
            include = t.watched;
            break;
        case TelegramFilter::UnknownOnly:
            include = !t.watched;
            break;
        case TelegramFilter::DuplicatesOnly:
            include = t.duplicate;
            break;
        case TelegramFilter::CrcFailOnly:
            include = !t.crc_ok;
            break;
        }
        if (include) {
            out.push_back(t);
        }
    }
    return out;
}

common::Result<void> MeterRegistry::upsert_watchlist(const WatchlistEntry& entry) {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    if (entry.key.empty()) {
        return common::Result<void>::error(common::ErrorCode::InvalidArgument);
    }

    RegistryState& s = state();
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        int idx = watchlist_index_by_key(s.watchlist, entry.key);
        if (idx < 0) {
            if (s.watchlist.size() >= MeterRegistry::kMaxWatchlistSize) {
                return common::Result<void>::error(common::ErrorCode::BufferFull);
            }
            s.watchlist.push_back(entry);
        } else {
            s.watchlist[static_cast<size_t>(idx)] = entry;
        }
        apply_watchlist_to_detected_locked();
    }

    return persist_watchlist();
}

common::Result<void> MeterRegistry::remove_watchlist(const std::string& key) {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    RegistryState& s = state();
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        s.watchlist.erase(std::remove_if(s.watchlist.begin(), s.watchlist.end(),
                                         [&key](const WatchlistEntry& e) { return e.key == key; }),
                          s.watchlist.end());
        apply_watchlist_to_detected_locked();
    }
    return persist_watchlist();
}

std::string MeterRegistry::derive_meter_key(const wmbus_link::ValidatedTelegram& telegram) {
    return telegram.identity_key();
}

std::string MeterRegistry::escape_field(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\' || c == '|' || c == '\n' || c == '\r') {
            out += '\\';
            if (c == '\n') {
                out += 'n';
            } else if (c == '\r') {
                out += 'r';
            } else {
                out += c;
            }
        } else {
            out += c;
        }
    }
    return out;
}

std::string MeterRegistry::unescape_field(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool esc = false;
    for (char c : s) {
        if (!esc && c == '\\') {
            esc = true;
            continue;
        }
        if (esc) {
            if (c == 'n') {
                out += '\n';
            } else if (c == 'r') {
                out += '\r';
            } else {
                out += c;
            }
            esc = false;
            continue;
        }
        out += c;
    }
    return out;
}

std::string MeterRegistry::serialize_watchlist_line(const WatchlistEntry& entry) {
    return escape_field(entry.key) + "|" + (entry.enabled ? "1" : "0") + "|" +
           escape_field(entry.alias) + "|" + escape_field(entry.note);
}

bool MeterRegistry::parse_watchlist_line(const std::string& line, WatchlistEntry& out) {
    std::vector<std::string> parts;
    std::string cur;
    bool esc = false;
    for (char c : line) {
        if (!esc && c == '\\') {
            esc = true;
            cur += c;
            continue;
        }
        if (!esc && c == '|') {
            parts.push_back(cur);
            cur.clear();
            continue;
        }
        esc = false;
        cur += c;
    }
    parts.push_back(cur);
    if (parts.size() < 4) {
        return false;
    }
    out.key = unescape_field(parts[0]);
    out.enabled = parts[1] == "1";
    out.alias = unescape_field(parts[2]);
    out.note = unescape_field(parts[3]);
    return !out.key.empty();
}

common::Result<void> MeterRegistry::load_watchlist() {
    RegistryState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);

    s.watchlist.clear();
    auto& storage = storage_service::StorageService::instance();
    if (!storage.is_mounted()) {
        return common::Result<void>::ok();
    }

    auto file_result = storage.read_file(kWatchlistFile);
    if (file_result.is_error()) {
        return common::Result<void>::ok();
    }

    std::istringstream in(file_result.value());
    std::string line;
    while (std::getline(in, line)) {
        WatchlistEntry e{};
        if (parse_watchlist_line(line, e)) {
            if (s.watchlist.size() >= MeterRegistry::kMaxWatchlistSize) {
                break;
            }
            s.watchlist.push_back(std::move(e));
        }
    }
    apply_watchlist_to_detected_locked();
    return common::Result<void>::ok();
}

common::Result<void> MeterRegistry::persist_watchlist() const {
    RegistryState& s = state();
    std::vector<WatchlistEntry> snapshot;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        snapshot = s.watchlist;
    }

    std::string file;
    for (const auto& e : snapshot) {
            file += serialize_watchlist_line(e);
            file += "\n";
    }

    auto& storage = storage_service::StorageService::instance();
    if (!storage.is_mounted()) {
        return common::Result<void>::ok();
    }
    return storage.write_file(kWatchlistFile, file.c_str(), file.size());
}

void MeterRegistry::apply_watchlist_to_detected_locked() {
    RegistryState& s = state();
    std::unordered_map<std::string, WatchlistEntry> map;
    map.reserve(s.watchlist.size());
    for (const auto& e : s.watchlist) {
        map[e.key] = e;
    }

    for (auto& m : s.detected) {
        auto it = map.find(m.key);
        if (it == map.end()) {
            m.watched = false;
            m.watch_enabled = false;
            m.alias.clear();
            m.note.clear();
            continue;
        }
        m.watched = true;
        m.watch_enabled = it->second.enabled;
        m.alias = it->second.alias;
        m.note = it->second.note;
    }
}

} // namespace meter_registry
