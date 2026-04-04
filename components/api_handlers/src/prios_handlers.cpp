#include "api_handlers/api_handlers_common.hpp"

#ifndef HOST_TEST_BUILD

#include "config_store/config_store.hpp"
#include "protocol_driver/protocol_ids.hpp"
#include "wmbus_prios_rx/prios_capture_service.hpp"
#include "wmbus_prios_rx/prios_fixture.hpp"
#include "wmbus_prios_rx/prios_analyzer.hpp"

namespace api_handlers::detail {

// GET /api/diagnostics/prios
//
// Returns PRIOS R3 bring-up status and the last N bounded raw captures.
// The "decoding" field is always false at this stage — this endpoint exists
// to support evidence gathering, not production frame routing.
//
// Response shape:
// {
//   "mode": "capture",
//   "campaign_active": true,
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
//       "prefix_hex": "A1B2C3..."
//     }
//   ]
// }

esp_err_t handle_diagnostics_prios(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
    }

    const auto snap = wmbus_prios_rx::PriosCaptureService::instance().snapshot();
    const auto cfg  = config_store::ConfigStore::instance().config();

    JsonPtr root = make_json_object();
    if (!root) {
        return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
    }

    cJSON_AddStringToObject(root.get(), "mode", "capture");
    cJSON_AddBoolToObject(root.get(), "campaign_active", cfg.radio.prios_capture_campaign);
    cJSON_AddStringToObject(root.get(), "variant",
                            cfg.radio.prios_manchester_enabled ? "manchester_on" : "manchester_off");
    cJSON_AddStringToObject(root.get(), "profile",
                            protocol_driver::radio_profile_id_to_string(
                                protocol_driver::RadioProfileId::WMbusPriosR3));
    cJSON_AddFalseToObject(root.get(),  "decoding");
    cJSON_AddNumberToObject(root.get(), "total_captures",
                            static_cast<double>(snap.total_inserted));
    cJSON_AddNumberToObject(root.get(), "total_evicted",
                            static_cast<double>(snap.total_evicted));

    cJSON* arr = cJSON_AddArrayToObject(root.get(), "recent_captures");
    for (size_t i = 0; i < snap.count; ++i) {
        const auto& r = snap.records[i];
        cJSON* rec = cJSON_CreateObject();
        cJSON_AddNumberToObject(rec, "seq",           static_cast<double>(r.sequence));
        cJSON_AddNumberToObject(rec, "timestamp_ms",  static_cast<double>(r.timestamp_ms));
        cJSON_AddNumberToObject(rec, "rssi_dbm",      static_cast<double>(r.rssi_dbm));
        cJSON_AddNumberToObject(rec, "lqi",           static_cast<double>(r.lqi));
        cJSON_AddNumberToObject(rec, "bytes_captured",
                                static_cast<double>(r.total_bytes_captured));
        cJSON_AddStringToObject(rec, "variant",
                                r.manchester_enabled ? "manchester_on" : "manchester_off");

        // Hex-encode the bounded prefix (at most 32 bytes).
        char hex_buf[wmbus_prios_rx::PriosCaptureRecord::kMaxPrefixBytes * 2 + 1]{};
        for (uint8_t j = 0; j < r.prefix_length; ++j) {
            constexpr char kHex[] = "0123456789ABCDEF";
            hex_buf[j * 2 + 0] = kHex[(r.prefix[j] >> 4) & 0x0F];
            hex_buf[j * 2 + 1] = kHex[r.prefix[j] & 0x0F];
        }
        hex_buf[r.prefix_length * 2] = '\0';
        cJSON_AddStringToObject(rec, "prefix_hex", hex_buf);

        cJSON_AddItemToArray(arr, rec);
    }

    return send_json_root(req, 200, root);
}

// GET /api/diagnostics/prios/export
//
// Returns all buffered captures as a JSON fixture array, ready to paste into a
// host-test source file.  One object per record; each object mirrors the fields
// of PriosFixtureFrame so callers can reconstruct them with minimal editing.
//
// Response shape:
// {
//   "count": 3,
//   "frames": [
//     {
//       "length": 18,
//       "rssi_dbm": -72,
//       "lqi": 90,
//       "radio_crc_ok": true,
//       "radio_crc_available": true,
//       "timestamp_ms": 12345678,
//       "hex": "A1B2C3..."
//     }
//   ]
// }

esp_err_t handle_diagnostics_prios_export(httpd_req_t* req) {
    const esp_err_t auth = require_auth(req);
    if (auth != ESP_OK) {
        return auth;
    }

    const auto snap = wmbus_prios_rx::PriosCaptureService::instance().snapshot();

    JsonPtr root = make_json_object();
    if (!root) {
        return send_json(req, 500, "{\"error\":\"out_of_memory\"}");
    }

    cJSON_AddNumberToObject(root.get(), "count", static_cast<double>(snap.count));

    cJSON* arr = cJSON_AddArrayToObject(root.get(), "frames");
    for (size_t i = 0; i < snap.count; ++i) {
        const auto& r = snap.records[i];

        // Build a PriosFixtureFrame from the live record for consistent serialisation.
        const wmbus_prios_rx::PriosFixtureFrame frame =
            wmbus_prios_rx::PriosFixtureFrame::from_record(r);

        cJSON* obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(obj, "length",    static_cast<double>(frame.length));
        cJSON_AddNumberToObject(obj, "rssi_dbm",  static_cast<double>(frame.rssi_dbm));
        cJSON_AddNumberToObject(obj, "lqi",       static_cast<double>(frame.lqi));
        cJSON_AddBoolToObject(obj,  "radio_crc_ok",        frame.radio_crc_ok);
        cJSON_AddBoolToObject(obj,  "radio_crc_available", frame.radio_crc_available);
        cJSON_AddNumberToObject(obj, "timestamp_ms", static_cast<double>(frame.timestamp_ms));
        // Record which PRIOS capture variant produced this frame.
        cJSON_AddStringToObject(obj, "variant",
                                r.manchester_enabled ? "manchester_on" : "manchester_off");

        // Full hex dump of the captured prefix.
        char hex_buf[wmbus_prios_rx::PriosFixtureFrame::kMaxBytes * 2 + 1]{};
        constexpr char kHex[] = "0123456789ABCDEF";
        for (uint8_t j = 0; j < frame.length; ++j) {
            hex_buf[j * 2 + 0] = kHex[(frame.bytes[j] >> 4) & 0x0F];
            hex_buf[j * 2 + 1] = kHex[frame.bytes[j] & 0x0F];
        }
        hex_buf[frame.length * 2] = '\0';
        cJSON_AddStringToObject(obj, "hex", hex_buf);

        cJSON_AddItemToArray(arr, obj);
    }

    return send_json_root(req, 200, root);
}

} // namespace api_handlers::detail

#endif // !HOST_TEST_BUILD
