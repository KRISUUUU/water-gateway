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
static constexpr size_t kMaxRecentTelegrams = 200;

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

void MeterRegistry::observe_frame(const wmbus_minimal_pipeline::WmbusFrame& frame, bool duplicate) {
    if (!initialized_) {
        return;
    }
    RegistryState& s = state();
    const std::string key = derive_meter_key(frame);

    std::lock_guard<std::mutex> lock(s.mutex);
    int idx = detected_index_by_key(s.detected, key);
    if (idx < 0) {
        DetectedMeter m{};
        m.key = key;
        m.manufacturer_id = frame.manufacturer_id();
        m.device_id = frame.device_id();
        m.device_type = frame.device_type();
        m.first_seen_ms = frame.metadata.timestamp_ms;
        m.last_seen_ms = frame.metadata.timestamp_ms;
        m.seen_count = 1;
        m.last_rssi_dbm = frame.metadata.rssi_dbm;
        m.last_lqi = frame.metadata.lqi;
        m.last_crc_ok = frame.metadata.crc_ok;
        s.detected.push_back(std::move(m));
        idx = static_cast<int>(s.detected.size() - 1);
    } else {
        DetectedMeter& m = s.detected[static_cast<size_t>(idx)];
        m.last_seen_ms = frame.metadata.timestamp_ms;
        m.seen_count++;
        m.device_type = frame.device_type();
        m.last_rssi_dbm = frame.metadata.rssi_dbm;
        m.last_lqi = frame.metadata.lqi;
        m.last_crc_ok = frame.metadata.crc_ok;
    }

    DetectedMeter& meter = s.detected[static_cast<size_t>(idx)];
    if (!frame.metadata.crc_ok) {
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
    t.timestamp_ms = frame.metadata.timestamp_ms;
    t.raw_hex = frame.raw_hex();
    t.frame_length = frame.metadata.frame_length;
    t.rssi_dbm = frame.metadata.rssi_dbm;
    t.lqi = frame.metadata.lqi;
    t.crc_ok = frame.metadata.crc_ok;
    t.duplicate = duplicate;
    t.meter_key = key;
    t.watched = watched;
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

std::string MeterRegistry::derive_meter_key(const wmbus_minimal_pipeline::WmbusFrame& frame) {
    return frame.identity_key();
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
            s.watchlist.push_back(std::move(e));
        }
    }
    apply_watchlist_to_detected_locked();
    return common::Result<void>::ok();
}

common::Result<void> MeterRegistry::persist_watchlist() const {
    RegistryState& s = state();
    std::string file;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        for (const auto& e : s.watchlist) {
            file += serialize_watchlist_line(e);
            file += "\n";
        }
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
