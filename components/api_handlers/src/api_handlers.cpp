#include "api_handlers/api_handlers.hpp"

#ifndef HOST_TEST_BUILD
#include "esp_http_server.h"
#include "esp_log.h"

namespace api_handlers::detail {
esp_err_t handle_bootstrap(httpd_req_t*); esp_err_t handle_bootstrap_setup(httpd_req_t*);
esp_err_t handle_auth_login(httpd_req_t*); esp_err_t handle_auth_logout(httpd_req_t*); esp_err_t handle_auth_password(httpd_req_t*);
esp_err_t handle_config_get(httpd_req_t*); esp_err_t handle_config_post(httpd_req_t*);
esp_err_t handle_diagnostics_radio(httpd_req_t*); esp_err_t handle_diagnostics_mqtt(httpd_req_t*);
esp_err_t handle_telegrams(httpd_req_t*); esp_err_t handle_meters_detected(httpd_req_t*); esp_err_t handle_watchlist_get(httpd_req_t*);
esp_err_t handle_watchlist_post(httpd_req_t*); esp_err_t handle_watchlist_delete(httpd_req_t*);
esp_err_t handle_ota_status(httpd_req_t*); esp_err_t handle_ota_upload(httpd_req_t*); esp_err_t handle_ota_url(httpd_req_t*);
esp_err_t handle_logs(httpd_req_t*); esp_err_t handle_support_bundle(httpd_req_t*); esp_err_t handle_system_reboot(httpd_req_t*); esp_err_t handle_system_factory_reset(httpd_req_t*);
esp_err_t handle_status(httpd_req_t*); esp_err_t handle_status_full(httpd_req_t*);
} // namespace api_handlers::detail

namespace {
constexpr const char* TAG = "api_handlers";
struct Route { httpd_method_t method; const char* uri; esp_err_t (*handler)(httpd_req_t*); };
esp_err_t register_uri(httpd_handle_t server, const Route& route) { httpd_uri_t u{}; u.uri = route.uri; u.method = route.method; u.handler = route.handler; const esp_err_t err = httpd_register_uri_handler(server, &u); if (err != ESP_OK) { ESP_LOGE(TAG, "Failed to register %s: %d", route.uri, err); } return err; }
constexpr Route kRoutes[] = {
    {HTTP_GET, "/api/bootstrap", api_handlers::detail::handle_bootstrap}, {HTTP_POST, "/api/bootstrap/setup", api_handlers::detail::handle_bootstrap_setup}, {HTTP_POST, "/api/auth/login", api_handlers::detail::handle_auth_login}, {HTTP_POST, "/api/auth/logout", api_handlers::detail::handle_auth_logout}, {HTTP_POST, "/api/auth/password", api_handlers::detail::handle_auth_password}, {HTTP_GET, "/api/status", api_handlers::detail::handle_status}, {HTTP_GET, "/api/status/full", api_handlers::detail::handle_status_full}, {HTTP_GET, "/api/telegrams", api_handlers::detail::handle_telegrams}, {HTTP_GET, "/api/meters/detected", api_handlers::detail::handle_meters_detected}, {HTTP_GET, "/api/watchlist", api_handlers::detail::handle_watchlist_get}, {HTTP_POST, "/api/watchlist", api_handlers::detail::handle_watchlist_post}, {HTTP_POST, "/api/watchlist/delete", api_handlers::detail::handle_watchlist_delete}, {HTTP_GET, "/api/diagnostics/radio", api_handlers::detail::handle_diagnostics_radio}, {HTTP_GET, "/api/diagnostics/mqtt", api_handlers::detail::handle_diagnostics_mqtt}, {HTTP_GET, "/api/config", api_handlers::detail::handle_config_get}, {HTTP_POST, "/api/config", api_handlers::detail::handle_config_post}, {HTTP_GET, "/api/ota/status", api_handlers::detail::handle_ota_status}, {HTTP_POST, "/api/ota/upload", api_handlers::detail::handle_ota_upload}, {HTTP_POST, "/api/ota/url", api_handlers::detail::handle_ota_url}, {HTTP_GET, "/api/logs", api_handlers::detail::handle_logs}, {HTTP_GET, "/api/support-bundle", api_handlers::detail::handle_support_bundle}, {HTTP_POST, "/api/system/reboot", api_handlers::detail::handle_system_reboot}, {HTTP_POST, "/api/system/factory-reset", api_handlers::detail::handle_system_factory_reset},
};
} // namespace
#endif

namespace api_handlers {

void register_all_handlers(void* server) {
#ifndef HOST_TEST_BUILD
    const httpd_handle_t srv = reinterpret_cast<httpd_handle_t>(server);
    if (!srv) { ESP_LOGE(TAG, "register_all_handlers: null server"); return; }
    for (const auto& route : kRoutes) { if (register_uri(srv, route) != ESP_OK) { return; } }
#else
    (void)server;
#endif
}

} // namespace api_handlers
