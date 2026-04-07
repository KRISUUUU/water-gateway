#include "host_test_stubs.hpp"
#include "wmbus_prios_rx/prios_export.hpp"
#include "wmbus_prios_rx/prios_capture_service.hpp"

#include "cJSON.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace wmbus_prios_rx;

namespace {

void test_export_json_contains_full_bounded_payload_and_metadata() {
    PriosCaptureRecord record{};
    record.sequence = 42;
    record.timestamp_ms = 1712345678;
    record.rssi_dbm = -71;
    record.lqi = 93;
    record.radio_crc_ok = true;
    record.radio_crc_available = true;
    record.total_bytes_captured = PriosCaptureRecord::kMaxCaptureBytes;
    record.manchester_enabled = true;
    for (size_t i = 0; i < PriosCaptureRecord::kMaxCaptureBytes; ++i) {
        record.captured_bytes[i] = static_cast<uint8_t>(i);
    }

    std::string json;
    append_export_json_object(json, record);

    cJSON* root = cJSON_Parse(json.c_str());
    assert(root != nullptr);
    assert(cJSON_GetObjectItemCaseSensitive(root, "total_bytes_captured")->valueint ==
           static_cast<int>(PriosCaptureRecord::kMaxCaptureBytes));
    assert(cJSON_GetObjectItemCaseSensitive(root, "length")->valueint ==
           static_cast<int>(PriosCaptureRecord::kMaxCaptureBytes));
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "radio_crc_ok")));
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "radio_crc_available")));

    const cJSON* variant = cJSON_GetObjectItemCaseSensitive(root, "variant");
    assert(cJSON_IsString(variant));
    assert(std::strcmp(variant->valuestring, "manchester_on") == 0);

    // device_fingerprint: record is long enough and exports bytes 4-7.
    const cJSON* fp_field = cJSON_GetObjectItemCaseSensitive(root, "device_fingerprint");
    assert(fp_field != nullptr);
    assert(cJSON_IsString(fp_field));
    assert(std::strlen(fp_field->valuestring) ==
           static_cast<size_t>(PriosDeviceFingerprint::kLength) * 2U);
    assert(std::strcmp(fp_field->valuestring, "04050607") == 0);

    const cJSON* prefix = cJSON_GetObjectItemCaseSensitive(root, "display_prefix_hex");
    assert(cJSON_IsString(prefix));
    assert(std::strlen(prefix->valuestring) ==
           PriosCaptureRecord::kDisplayPrefixBytes * 2U);

    const cJSON* full_hex = cJSON_GetObjectItemCaseSensitive(root, "full_hex");
    const cJSON* alias_hex = cJSON_GetObjectItemCaseSensitive(root, "hex");
    assert(cJSON_IsString(full_hex));
    assert(cJSON_IsString(alias_hex));
    assert(std::strlen(full_hex->valuestring) ==
           PriosCaptureRecord::kMaxCaptureBytes * 2U);
    assert(std::strcmp(full_hex->valuestring, alias_hex->valuestring) == 0);
    assert(std::strncmp(full_hex->valuestring, "00010203", 8) == 0);

    cJSON_Delete(root);
    std::printf("  PASS: export JSON includes full bounded payload and metadata\n");
}

void test_export_json_length_tracks_partial_capture() {
    PriosCaptureRecord record{};
    record.total_bytes_captured = 5;
    record.captured_bytes[0] = 0xAA;
    record.captured_bytes[1] = 0xBB;
    record.captured_bytes[2] = 0xCC;
    record.captured_bytes[3] = 0xDD;
    record.captured_bytes[4] = 0xEE;

    std::string json;
    append_export_json_object(json, record);

    cJSON* root = cJSON_Parse(json.c_str());
    assert(root != nullptr);

    const cJSON* length = cJSON_GetObjectItemCaseSensitive(root, "length");
    const cJSON* full_hex = cJSON_GetObjectItemCaseSensitive(root, "full_hex");
    const cJSON* prefix = cJSON_GetObjectItemCaseSensitive(root, "display_prefix_hex");
    assert(cJSON_IsNumber(length));
    assert(length->valueint == 5);
    assert(cJSON_IsString(full_hex));
    assert(cJSON_IsString(prefix));
    assert(std::strcmp(full_hex->valuestring, "AABBCCDDEE") == 0);
    assert(std::strcmp(prefix->valuestring, "AABBCCDDEE") == 0);

    // device_fingerprint: capture is 5 bytes, kOffset+kLength=15 required → null.
    const cJSON* fp_null = cJSON_GetObjectItemCaseSensitive(root, "device_fingerprint");
    assert(fp_null != nullptr);
    assert(cJSON_IsNull(fp_null));

    cJSON_Delete(root);
    std::printf("  PASS: export JSON keeps partial capture length exact\n");
}

} // namespace

int main() {
    std::printf("=== test_prios_export ===\n");
    test_export_json_contains_full_bounded_payload_and_metadata();
    test_export_json_length_tracks_partial_capture();
    std::printf("All PRIOS export tests passed.\n");
    return 0;
}
