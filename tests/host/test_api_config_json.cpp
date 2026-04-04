#include "host_test_stubs.hpp"
#include "api_handlers/config_json_codec.hpp"
#include "config_store/config_models.hpp"
#include "cJSON.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace config_store;
using namespace protocol_driver;

namespace {

void set_valid_mqtt_host(AppConfig& cfg) {
    std::strncpy(cfg.mqtt.host, "broker", sizeof(cfg.mqtt.host) - 1);
}

void test_config_json_includes_radio_scheduler_and_prios_fields() {
    AppConfig cfg = AppConfig::make_default();
    cfg.radio.scheduler_mode = RadioSchedulerMode::Scan;
    cfg.radio.enabled_profiles =
        static_cast<RadioProfileMask>(kRadioProfileMaskWMbusT868 | kRadioProfileMaskWMbusPriosR3);
    cfg.radio.prios_capture_campaign = true;
    cfg.radio.prios_manchester_enabled = true;

    const std::string json = api_handlers::detail::config_to_json_redacted(cfg);
    cJSON* root = cJSON_Parse(json.c_str());
    assert(root != nullptr);
    cJSON* radio = cJSON_GetObjectItemCaseSensitive(root, "radio");
    assert(radio != nullptr);
    assert(cJSON_GetObjectItemCaseSensitive(radio, "scheduler_mode") != nullptr);
    assert(cJSON_GetObjectItemCaseSensitive(radio, "enabled_profiles") != nullptr);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(radio, "prios_capture_campaign")));
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(radio, "prios_manchester_enabled")));
    assert(static_cast<int>(cJSON_GetObjectItemCaseSensitive(radio, "scheduler_mode")->valuedouble) ==
           static_cast<int>(RadioSchedulerMode::Scan));
    assert(static_cast<int>(cJSON_GetObjectItemCaseSensitive(radio, "enabled_profiles")->valuedouble) ==
           static_cast<int>(cfg.radio.enabled_profiles));
    cJSON_Delete(root);
    std::printf("  PASS: config JSON exposes scheduler and PRIOS radio fields\n");
}

void test_apply_config_json_updates_radio_scheduler_and_prios_fields() {
    AppConfig cfg = AppConfig::make_default();
    set_valid_mqtt_host(cfg);

    cJSON* root = cJSON_CreateObject();
    cJSON* radio = cJSON_AddObjectToObject(root, "radio");
    cJSON_AddNumberToObject(radio, "scheduler_mode",
                            static_cast<double>(RadioSchedulerMode::Priority));
    cJSON_AddNumberToObject(
        radio, "enabled_profiles",
        static_cast<double>(kRadioProfileMaskWMbusT868 | kRadioProfileMaskWMbusPriosR3));
    cJSON_AddBoolToObject(radio, "prios_capture_campaign", true);
    cJSON_AddBoolToObject(radio, "prios_manchester_enabled", true);

    api_handlers::detail::apply_config_json(root, cfg);
    cJSON_Delete(root);

    assert(cfg.radio.scheduler_mode == RadioSchedulerMode::Priority);
    assert(cfg.radio.enabled_profiles ==
           static_cast<RadioProfileMask>(kRadioProfileMaskWMbusT868 | kRadioProfileMaskWMbusPriosR3));
    assert(cfg.radio.prios_capture_campaign);
    assert(cfg.radio.prios_manchester_enabled);
    std::printf("  PASS: apply_config_json updates scheduler and PRIOS radio fields\n");
}

void test_apply_config_json_accepts_profile_name_array() {
    AppConfig cfg = AppConfig::make_default();
    set_valid_mqtt_host(cfg);

    cJSON* root = cJSON_CreateObject();
    cJSON* radio = cJSON_AddObjectToObject(root, "radio");
    cJSON_AddStringToObject(radio, "scheduler_mode", "scan");
    cJSON* profiles = cJSON_AddArrayToObject(radio, "enabled_profiles");
    cJSON_AddItemToArray(profiles, cJSON_CreateString("WMbusT868"));
    cJSON_AddItemToArray(profiles, cJSON_CreateString("prios_r3"));

    api_handlers::detail::apply_config_json(root, cfg);
    cJSON_Delete(root);

    assert(cfg.radio.scheduler_mode == RadioSchedulerMode::Scan);
    assert(cfg.radio.enabled_profiles ==
           static_cast<RadioProfileMask>(kRadioProfileMaskWMbusT868 | kRadioProfileMaskWMbusPriosR3));
    std::printf("  PASS: apply_config_json accepts scheduler names and profile arrays\n");
}

} // namespace

int main() {
    std::printf("=== test_api_config_json ===\n");
    test_config_json_includes_radio_scheduler_and_prios_fields();
    test_apply_config_json_updates_radio_scheduler_and_prios_fields();
    test_apply_config_json_accepts_profile_name_array();
    std::printf("All API config JSON tests passed.\n");
    return 0;
}
