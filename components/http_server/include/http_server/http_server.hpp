#pragma once

#include "common/result.hpp"

#ifndef HOST_TEST_BUILD
#include "esp_http_server.h"
#endif

namespace http_server {

enum class HttpServerState {
    Uninitialized = 0,
    Stopped,
    Running,
};

// Singleton HTTP server: lifecycle, SPIFFS-backed static assets under
// /storage/web/, and Bearer-token authorization helper for API routes.
//
// Typical sequence: initialize() → start(port) → register_uri_handler(...) for
// /api/* (see api_handlers) → register_static_web_handler() for GET /* →
// stop() on shutdown. Specific URI handlers must be registered before the
// catch-all static handler so /api wins over /*.
class HttpServer {
public:
    static HttpServer& instance();

    common::Result<void> initialize();

#ifndef HOST_TEST_BUILD
    common::Result<void> start(uint16_t port);

    common::Result<void> register_uri_handler(const httpd_uri_t& uri);

    // Registers GET /* → files under /storage/web/ (SPIFFS mount point /storage).
    common::Result<void> register_static_web_handler();

    [[nodiscard]] httpd_handle_t native_handle() const { return server_; }

    // Returns true when Authorization: Bearer <token> is present and
    // AuthService::validate_session accepts the token.
    bool authorize_request(httpd_req_t* req);
#else
    common::Result<void> start(uint16_t port);
#endif

    common::Result<void> stop();

    [[nodiscard]] HttpServerState state() const;

private:
    HttpServer() = default;

#ifndef HOST_TEST_BUILD
    httpd_handle_t server_{nullptr};
    uint16_t port_{80};
#endif

    bool initialized_{false};
    HttpServerState state_{HttpServerState::Uninitialized};
};

} // namespace http_server
