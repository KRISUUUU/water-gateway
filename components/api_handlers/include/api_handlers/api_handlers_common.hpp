#pragma once

#ifndef HOST_TEST_BUILD

#include "auth_service/auth_service.hpp"
#include "config_store/config_models.hpp"
#include "config_store/config_store.hpp"
#include "config_store/config_validation.hpp"
#include "mqtt_service/mqtt_service.hpp"
#include "ota_manager/ota_manager.hpp"
#include "persistent_log_buffer/persistent_log_buffer.hpp"
#include "radio_cc1101/radio_cc1101.hpp"
#include "radio_state_machine/radio_state_machine.hpp"
#include "wifi_manager/wifi_manager.hpp"

#include "cJSON.h"
#include "esp_http_server.h"

#include <memory>
#include <string>

namespace api_handlers::detail {

using JsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
using JsonStringPtr = std::unique_ptr<char, decltype(&cJSON_free)>;

JsonPtr make_json_object();
std::string to_unformatted_json(cJSON* root);

esp_err_t send_json(httpd_req_t* req, int status_code, const char* body);
esp_err_t send_json_root(httpd_req_t* req, int status_code, cJSON* root);
esp_err_t send_json_root(httpd_req_t* req, int status_code, const JsonPtr& root);
esp_err_t send_json_chunk_row(httpd_req_t* req, std::string& row);
esp_err_t send_validation_issues(httpd_req_t* req, const config_store::ValidationResult& vr);
esp_err_t require_auth(httpd_req_t* req);

bool read_request_body(httpd_req_t* req, std::string& out, size_t max_len);
bool request_content_type_is_json(httpd_req_t* req);
bool request_content_type_is_binary(httpd_req_t* req);
void apply_json_security_headers(httpd_req_t* req);
void apply_config_json(const cJSON* root, config_store::AppConfig& cfg);
void assign_admin_password_hash(config_store::AuthConfig& auth, const char* hash_cstr);

bool has_https_scheme(const std::string& url);
std::string query_param(const char* uri, const char* key);
void json_escape_append(std::string& out, const std::string& s);
bool is_hex_string(const std::string& s);
uint32_t client_id_from_request(httpd_req_t* req);

const char* reset_reason_str(unsigned int code);
const char* log_severity_name(persistent_log_buffer::LogSeverity s);
const char* mqtt_state_name(mqtt_service::MqttState s);
const char* rsm_state_name(radio_state_machine::RsmState s);
const char* wifi_state_name(wifi_manager::WifiState s);
const char* radio_state_name(radio_cc1101::RadioState s);
const char* ota_state_name(ota_manager::OtaState s);
const char* config_load_source_name(config_store::ConfigLoadSource s);

std::string config_to_json_redacted(const config_store::AppConfig& c);

} // namespace api_handlers::detail

#endif // !HOST_TEST_BUILD
