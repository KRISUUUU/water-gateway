#include "api_handlers/api_handlers_common.hpp"

#ifndef HOST_TEST_BUILD

#include "config_store/config_store.hpp"
#include "meter_registry/meter_registry.hpp"
#include "protocol_driver/protocol_ids.hpp"
#include "wmbus_prios_rx/prios_analyzer.hpp"
#include "wmbus_prios_rx/prios_bringup_session.hpp"
#include "wmbus_prios_rx/prios_capture_service.hpp"
#include "wmbus_prios_rx/prios_export.hpp"

#include <unordered_map>

namespace api_handlers::detail {

namespace {

esp_err_t begin_prios_export_response(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");
    apply_json_security_headers(req);
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "attachment; filename=\"prios-captures.json\"");
    return httpd_resp_send_chunk(req, "{\"count\":", HTTPD_RESP_USE_STRLEN);
}

} // namespace

// GET /api/diagnostics/prios
//
// Returns PRIOS R3 bring-up status and the last N bounded raw captures.
// The "decoding" field is always false at this stage — this endpoint exists
// to support evidence gathering, not production frame routing.
//
// Response shape:
// {
//   "mode": "campaign",
//   "campaign_active": true,
//   "discovery_active": false,
//   "variant": "manchester_off",
//   "profile": "WMbusPriosR3",
//   "decoding": false,
//   "total_captures": 42,
//   "total_evicted": 0,
//   "recent_captures": [
//     {
//       "seq": 1,
//       "timestamp_ms": 12345678,
//       "rssi_dbm": -72,
//       "lqi": 90,
//       "bytes_captured": 32,
//       "variant": "manchester_off",
//       "display_prefix_hex": "A1B2C3..."
//     }
//   ]
// }

esp_err_t handle_diagnostics_prios(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
    }

    const auto preview = wmbus_prios_rx::PriosCaptureService::instance().preview_snapshot();
    const auto stats   = wmbus_prios_rx::PriosCaptureService::instance().stats();
    const auto cfg     = config_store::ConfigStore::instance().config();

    JsonPtr root = make_json_object();
    if (!root) {
        return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
    }

    const bool discovery_active = cfg.radio.prios_discovery_mode;
    const bool campaign_active = cfg.radio.prios_capture_campaign;
    const char* mode = "inactive";
    if (discovery_active) {
        mode = "discovery_sniffer";
    } else if (campaign_active) {
        mode = "campaign_1E9B";
    }

    const double retained_average_length =
        stats.total_inserted > 0
            ? static_cast<double>(stats.retained_length_total) /
                  static_cast<double>(stats.total_inserted)
            : 0.0;

    cJSON_AddStringToObject(root.get(), "mode", mode);
    cJSON_AddBoolToObject(root.get(), "campaign_active", cfg.radio.prios_capture_campaign);
    cJSON_AddBoolToObject(root.get(), "discovery_active", cfg.radio.prios_discovery_mode);
    cJSON_AddStringToObject(root.get(), "variant",
                            cfg.radio.prios_manchester_enabled ? "manchester_on" : "manchester_off");
    cJSON_AddStringToObject(root.get(), "profile",
                            protocol_driver::radio_profile_id_to_string(
                                protocol_driver::RadioProfileId::WMbusPriosR3));
    cJSON_AddFalseToObject(root.get(),  "decoding");
    cJSON_AddNumberToObject(root.get(), "total_captures",
                            static_cast<double>(stats.total_inserted));
    cJSON_AddNumberToObject(root.get(), "total_evicted",
                            static_cast<double>(stats.total_evicted));
    cJSON_AddNumberToObject(root.get(), "retained_captures",
                            static_cast<double>(stats.count));
    cJSON_AddNumberToObject(root.get(), "burst_starts_seen",
                            static_cast<double>(stats.total_burst_starts));
    cJSON_AddNumberToObject(root.get(), "sync_campaign_starts",
                            static_cast<double>(stats.total_sync_campaign_starts));
    cJSON_AddNumberToObject(root.get(), "total_dedup_rejected",
                            static_cast<double>(stats.total_dedup_rejected));
    cJSON_AddNumberToObject(root.get(), "total_device_quota_rejected",
                            static_cast<double>(stats.total_device_quota_rejected));
    cJSON_AddNumberToObject(root.get(), "recent_preview_count",
                            static_cast<double>(preview.count));
    cJSON_AddNumberToObject(root.get(), "noise_rejections",
                            static_cast<double>(stats.total_noise_rejected));
    cJSON_AddNumberToObject(root.get(), "quality_rejections",
                            static_cast<double>(stats.total_quality_rejected));
    cJSON_AddNumberToObject(root.get(), "variant_b_short_rejections",
                            static_cast<double>(stats.variant_b_short_rejected));
    cJSON_AddNumberToObject(root.get(), "similarity_rejections",
                            static_cast<double>(stats.total_similarity_rejected));
    cJSON_AddNumberToObject(root.get(), "retained_variant_a_total",
                            static_cast<double>(stats.retained_variant_a_total));
    cJSON_AddNumberToObject(root.get(), "retained_variant_b_total",
                            static_cast<double>(stats.retained_variant_b_total));
    cJSON_AddNumberToObject(root.get(), "retained_length_avg",
                            retained_average_length);
    cJSON_AddNumberToObject(root.get(), "retained_length_min",
                            static_cast<double>(stats.retained_length_min));
    cJSON_AddNumberToObject(root.get(), "retained_length_max",
                            static_cast<double>(stats.retained_length_max));
    cJSON_AddNumberToObject(root.get(), "variant_b_min_timeout_capture_bytes",
                            static_cast<double>(
                                wmbus_prios_rx::PriosBringUpSession::kVariantBMinTimeoutCaptureBytes));

    // Build fingerprint → alias lookup from watchlist.
    // PRIOS devices are stored in the watchlist with the fingerprint hex as their key,
    // so users can assign human-readable names via the watchlist quick-add flow.
    std::unordered_map<std::string, std::string> fp_alias_map;
    for (const auto& entry : meter_registry::MeterRegistry::instance().watchlist()) {
        if (!entry.alias.empty()) {
            fp_alias_map[entry.key] = entry.alias;
        }
    }

    constexpr char kHex[] = "0123456789ABCDEF";

    cJSON* arr = cJSON_AddArrayToObject(root.get(), "recent_captures");
    for (size_t i = 0; i < preview.count; ++i) {
        const auto& r = preview.records[i];
        cJSON* rec = cJSON_CreateObject();
        cJSON_AddNumberToObject(rec, "seq",           static_cast<double>(r.sequence));
        cJSON_AddNumberToObject(rec, "timestamp_ms",  static_cast<double>(r.timestamp_ms));
        cJSON_AddNumberToObject(rec, "rssi_dbm",      static_cast<double>(r.rssi_dbm));
        cJSON_AddNumberToObject(rec, "lqi",           static_cast<double>(r.lqi));
        cJSON_AddNumberToObject(rec, "bytes_captured",
                                static_cast<double>(r.total_bytes_captured));
        cJSON_AddStringToObject(rec, "variant",
                                r.manchester_enabled ? "manchester_on" : "manchester_off");

        // Device fingerprint (bytes 9–14 of the frame). Usable as a stable
        // per-device identity during bring-up before a full decoder is available.
        const auto fp = wmbus_prios_rx::PriosAnalyzer::extract_fingerprint(
            r.preview_bytes, r.preview_length);
        if (fp.valid) {
            char fp_hex[wmbus_prios_rx::PriosDeviceFingerprint::kLength * 2 + 1]{};
            for (uint8_t bi = 0; bi < wmbus_prios_rx::PriosDeviceFingerprint::kLength; ++bi) {
                fp_hex[bi * 2 + 0] = kHex[(fp.bytes[bi] >> 4) & 0x0F];
                fp_hex[bi * 2 + 1] = kHex[fp.bytes[bi] & 0x0F];
            }
            fp_hex[wmbus_prios_rx::PriosDeviceFingerprint::kLength * 2] = '\0';
            cJSON_AddStringToObject(rec, "device_fingerprint", fp_hex);

            // Alias from watchlist (if user has already named this device).
            auto alias_it = fp_alias_map.find(fp_hex);
            if (alias_it != fp_alias_map.end()) {
                cJSON_AddStringToObject(rec, "device_alias", alias_it->second.c_str());
            } else {
                cJSON_AddNullToObject(rec, "device_alias");
            }
        } else {
            cJSON_AddNullToObject(rec, "device_fingerprint");
            cJSON_AddNullToObject(rec, "device_alias");
        }

        // Short dashboard preview only. Export uses the full bounded payload.
        char hex_buf[wmbus_prios_rx::PriosCaptureRecord::kDisplayPrefixBytes * 2 + 1]{};
        for (uint8_t j = 0; j < r.preview_length; ++j) {
            hex_buf[j * 2 + 0] = kHex[(r.preview_bytes[j] >> 4) & 0x0F];
            hex_buf[j * 2 + 1] = kHex[r.preview_bytes[j] & 0x0F];
        }
        hex_buf[r.preview_length * 2] = '\0';
        cJSON_AddStringToObject(rec, "display_prefix_hex", hex_buf);

        cJSON_AddItemToArray(arr, rec);
    }

    return send_json_root(req, 200, root);
}

// GET /api/diagnostics/prios/export
//
// Returns all buffered captures as a JSON fixture array, ready for offline
// analysis or to paste into a host-test source file.
//
// Response shape:
// {
//   "count": 3,
//   "frames": [
//     {
//       "total_bytes_captured": 18,
//       "length": 18,
//       "rssi_dbm": -72,
//       "lqi": 90,
//       "radio_crc_ok": true,
//       "radio_crc_available": true,
//       "timestamp_ms": 12345678,
//       "variant": "manchester_off",
//       "display_prefix_hex": "A1B2C3...",
//       "full_hex": "A1B2C3..."
//     }
//   ]
// }

esp_err_t handle_diagnostics_prios_export(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
    }

    auto snap = wmbus_prios_rx::PriosCaptureService::instance().snapshot_allocated();
    if (!snap) {
        return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
    }

    esp_err_t err = begin_prios_export_response(req);
    if (err != ESP_OK) {
        return err;
    }

    std::string prefix = std::to_string(static_cast<unsigned long long>(snap->count));
    prefix += ",\"frames\":[";
    err = httpd_resp_send_chunk(req, prefix.c_str(), HTTPD_RESP_USE_STRLEN);
    if (err != ESP_OK) {
        return err;
    }

    std::string row;
    bool first = true;
    for (size_t i = 0; i < snap->count; ++i) {
        if (!first) {
            err = httpd_resp_send_chunk(req, ",", HTTPD_RESP_USE_STRLEN);
            if (err != ESP_OK) {
                return err;
            }
        }
        first = false;

        wmbus_prios_rx::append_export_json_object(row, snap->records[i]);
        err = send_json_chunk_row(req, row);
        if (err != ESP_OK) {
            return err;
        }
    }

    err = httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN);
    if (err != ESP_OK) {
        return err;
    }
    return httpd_resp_send_chunk(req, nullptr, 0);
}

} // namespace api_handlers::detail

#endif // !HOST_TEST_BUILD
