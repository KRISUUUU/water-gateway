#include "api_handlers/api_handlers_common.hpp"

#ifndef HOST_TEST_BUILD

#include "meter_registry/meter_registry.hpp"

namespace api_handlers::detail {

namespace {
esp_err_t begin_stream_array(httpd_req_t* req, const char* key) {
    httpd_resp_set_type(req, "application/json");
    apply_json_security_headers(req);
    httpd_resp_set_status(req, "200 OK");
    std::string prefix = "{\"";
    prefix += key;
    prefix += "\":[";
    return httpd_resp_send_chunk(req, prefix.c_str(), HTTPD_RESP_USE_STRLEN);
}
} // namespace

esp_err_t handle_telegrams(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
    }
    meter_registry::TelegramFilter filter = meter_registry::TelegramFilter::All;
    const std::string f = query_param(req->uri, "filter");
    if (f == "watched") filter = meter_registry::TelegramFilter::WatchedOnly;
    else if (f == "unknown") filter = meter_registry::TelegramFilter::UnknownOnly;
    else if (f == "duplicates") filter = meter_registry::TelegramFilter::DuplicatesOnly;
    else if (f == "crc_fail") filter = meter_registry::TelegramFilter::CrcFailOnly;

    const auto telegrams = meter_registry::MeterRegistry::instance().recent_telegrams(filter);
    esp_err_t err = begin_stream_array(req, "telegrams");
    if (err != ESP_OK) {
        return err;
    }

    bool first = true;
    std::string row;
    for (const auto& t : telegrams) {
        row.clear();
        row.reserve(256 + t.raw_hex.size() + t.meter_key.size());
        if (!first) row.push_back(',');
        first = false;
        row += "{\"timestamp_ms\":" + std::to_string(static_cast<long long>(t.timestamp_ms));
        row += ",\"raw_hex\":\"";
        if (is_hex_string(t.raw_hex)) row += t.raw_hex; else json_escape_append(row, t.raw_hex);
        row += "\",\"frame_length\":" + std::to_string(static_cast<unsigned int>(t.frame_length));
        row += ",\"rssi_dbm\":" + std::to_string(static_cast<int>(t.rssi_dbm));
        row += ",\"lqi\":" + std::to_string(static_cast<unsigned int>(t.lqi));
        row += ",\"crc_ok\":";
        row += t.crc_ok ? "true" : "false";
        row += ",\"duplicate\":";
        row += t.duplicate ? "true" : "false";
        row += ",\"meter_key\":\"";
        json_escape_append(row, t.meter_key);
        row += "\",\"watched\":";
        row += t.watched ? "true" : "false";
        row += '}';
        err = send_json_chunk_row(req, row);
        if (err != ESP_OK) {
            return err;
        }
    }
    return httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN) == ESP_OK
               ? httpd_resp_send_chunk(req, nullptr, 0)
               : ESP_FAIL;
}

esp_err_t handle_meters_detected(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
    }
    const auto meters = meter_registry::MeterRegistry::instance().detected_meters();
    const std::string filter = query_param(req->uri, "filter");
    esp_err_t err = begin_stream_array(req, "meters");
    if (err != ESP_OK) {
        return err;
    }

    bool first = true;
    std::string row;
    for (const auto& m : meters) {
        if ((filter == "watched" && !m.watched) || (filter == "unknown" && m.watched)) {
            continue;
        }
        row.clear();
        row.reserve(256 + m.key.size() + m.alias.size() + m.note.size());
        if (!first) row.push_back(',');
        first = false;
        row += "{\"key\":\"";
        json_escape_append(row, m.key);
        row += "\",\"manufacturer_id\":" + std::to_string(static_cast<unsigned int>(m.manufacturer_id));
        row += ",\"device_id\":" + std::to_string(static_cast<unsigned long long>(m.device_id));
        row += ",\"first_seen_ms\":" + std::to_string(static_cast<long long>(m.first_seen_ms));
        row += ",\"last_seen_ms\":" + std::to_string(static_cast<long long>(m.last_seen_ms));
        row += ",\"seen_count\":" + std::to_string(static_cast<unsigned long long>(m.seen_count));
        row += ",\"last_rssi_dbm\":" + std::to_string(static_cast<int>(m.last_rssi_dbm));
        row += ",\"last_lqi\":" + std::to_string(static_cast<unsigned int>(m.last_lqi));
        row += ",\"last_crc_ok\":";
        row += m.last_crc_ok ? "true" : "false";
        row += ",\"duplicate_count\":" + std::to_string(static_cast<unsigned long long>(m.duplicate_count));
        row += ",\"crc_fail_count\":" + std::to_string(static_cast<unsigned long long>(m.crc_fail_count));
        row += ",\"watched\":";
        row += m.watched ? "true" : "false";
        row += ",\"watch_enabled\":";
        row += m.watch_enabled ? "true" : "false";
        row += ",\"alias\":\"";
        json_escape_append(row, m.alias);
        row += "\",\"note\":\"";
        json_escape_append(row, m.note);
        row += "\"}";
        err = send_json_chunk_row(req, row);
        if (err != ESP_OK) {
            return err;
        }
    }
    return httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN) == ESP_OK
               ? httpd_resp_send_chunk(req, nullptr, 0)
               : ESP_FAIL;
}

esp_err_t handle_watchlist_get(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
    }
    const auto entries = meter_registry::MeterRegistry::instance().watchlist();
    esp_err_t err = begin_stream_array(req, "watchlist");
    if (err != ESP_OK) {
        return err;
    }

    bool first = true;
    std::string row;
    for (const auto& entry : entries) {
        row.clear();
        row.reserve(128 + entry.key.size() + entry.alias.size() + entry.note.size());
        if (!first) row.push_back(',');
        first = false;
        row += "{\"key\":\"";
        json_escape_append(row, entry.key);
        row += "\",\"enabled\":";
        row += entry.enabled ? "true" : "false";
        row += ",\"alias\":\"";
        json_escape_append(row, entry.alias);
        row += "\",\"note\":\"";
        json_escape_append(row, entry.note);
        row += "\"}";
        err = send_json_chunk_row(req, row);
        if (err != ESP_OK) {
            return err;
        }
    }
    return httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN) == ESP_OK
               ? httpd_resp_send_chunk(req, nullptr, 0)
               : ESP_FAIL;
}

esp_err_t handle_watchlist_post(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
    }
    if (!request_content_type_is_json(req)) {
        return send_json(req, 415,
                         "{\"error\":\"unsupported_content_type\","
                         "\"detail\":\"use application/json\"}");
    }
    std::string body;
    if (!read_request_body(req, body, 4096)) {
        return send_json(req, 413, "{\"error\":\"body_too_large\"}");
    }
    JsonPtr root(cJSON_Parse(body.c_str()), cJSON_Delete);
    if (!root) {
        return send_json(req, 400, "{\"error\":\"invalid_json\"}");
    }

    meter_registry::WatchlistEntry entry{};
    const cJSON* key = cJSON_GetObjectItemCaseSensitive(root.get(), "key");
    const cJSON* enabled = cJSON_GetObjectItemCaseSensitive(root.get(), "enabled");
    const cJSON* alias = cJSON_GetObjectItemCaseSensitive(root.get(), "alias");
    const cJSON* note = cJSON_GetObjectItemCaseSensitive(root.get(), "note");
    if (key && cJSON_IsString(key) && key->valuestring) entry.key = key->valuestring;
    if (enabled && (cJSON_IsBool(enabled) || cJSON_IsNumber(enabled))) {
        entry.enabled = cJSON_IsTrue(enabled) || (cJSON_IsNumber(enabled) && enabled->valuedouble != 0);
    }
    if (alias && cJSON_IsString(alias) && alias->valuestring) entry.alias = alias->valuestring;
    if (note && cJSON_IsString(note) && note->valuestring) entry.note = note->valuestring;
    if (entry.key.empty()) {
        return send_json(req, 400, "{\"error\":\"missing_key\"}");
    }

    const auto result = meter_registry::MeterRegistry::instance().upsert_watchlist(entry);
    return result.is_ok() ? send_json(req, 200, "{\"ok\":true}")
                          : send_json(req, 500, "{\"error\":\"watchlist_save_failed\"}");
}

esp_err_t handle_watchlist_delete(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
    }
    if (!request_content_type_is_json(req)) {
        return send_json(req, 415,
                         "{\"error\":\"unsupported_content_type\","
                         "\"detail\":\"use application/json\"}");
    }
    std::string body;
    if (!read_request_body(req, body, 2048)) {
        return send_json(req, 413, "{\"error\":\"body_too_large\"}");
    }
    JsonPtr root(cJSON_Parse(body.c_str()), cJSON_Delete);
    if (!root) {
        return send_json(req, 400, "{\"error\":\"invalid_json\"}");
    }
    const cJSON* key = cJSON_GetObjectItemCaseSensitive(root.get(), "key");
    const std::string key_value =
        (key && cJSON_IsString(key) && key->valuestring) ? key->valuestring : "";
    if (key_value.empty()) {
        return send_json(req, 400, "{\"error\":\"missing_key\"}");
    }

    const auto result = meter_registry::MeterRegistry::instance().remove_watchlist(key_value);
    return result.is_ok() ? send_json(req, 200, "{\"ok\":true}")
                          : send_json(req, 500, "{\"error\":\"watchlist_delete_failed\"}");
}

} // namespace api_handlers::detail

#endif // !HOST_TEST_BUILD
