#include "http_server/http_server.hpp"

#ifndef HOST_TEST_BUILD
#include "auth_service/auth_service.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <strings.h>
#include "esp_http_server.h"
#include "esp_log.h"

static const char* TAG = "http_srv";
static const char* kWebRoot = "/storage/web";

static std::string uri_path_only(const char* uri) {
    if (!uri) {
        return "/";
    }
    std::string p(uri);
    const size_t q = p.find('?');
    if (q != std::string::npos) {
        p.resize(q);
    }
    if (p.empty()) {
        return "/";
    }
    return p;
}

static bool path_has_traversal(const std::string& rel) {
    return rel.find("..") != std::string::npos;
}

static const char* content_type_for_path(const char* path) {
    const char* ext = std::strrchr(path, '.');
    if (!ext) {
        return "application/octet-stream";
    }
    if (strcasecmp(ext, ".html") == 0) {
        return "text/html; charset=utf-8";
    }
    if (strcasecmp(ext, ".js") == 0) {
        return "application/javascript; charset=utf-8";
    }
    if (strcasecmp(ext, ".css") == 0) {
        return "text/css; charset=utf-8";
    }
    if (strcasecmp(ext, ".json") == 0) {
        return "application/json; charset=utf-8";
    }
    if (strcasecmp(ext, ".svg") == 0) {
        return "image/svg+xml";
    }
    if (strcasecmp(ext, ".png") == 0) {
        return "image/png";
    }
    if (strcasecmp(ext, ".ico") == 0) {
        return "image/x-icon";
    }
    return "application/octet-stream";
}

static esp_err_t static_spiffs_handler(httpd_req_t* req) {
    std::string rel = uri_path_only(req->uri);
    if (rel.empty() || rel[0] != '/') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad path");
        return ESP_FAIL;
    }
    if (path_has_traversal(rel)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return ESP_FAIL;
    }

    if (rel.size() == 1 && rel[0] == '/') {
        rel = "/index.html";
    } else if (!rel.empty() && rel.back() == '/') {
        rel += "index.html";
    }

    char fs_path[256];
    const int n = std::snprintf(fs_path, sizeof(fs_path), "%s%s", kWebRoot, rel.c_str());
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(fs_path)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Path too long");
        return ESP_FAIL;
    }

    FILE* f = std::fopen(fs_path, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_FAIL;
    }

    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > static_cast<long>(512 * 1024)) {
        std::fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "File too large");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, content_type_for_path(fs_path));
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=3600");

    if (sz == 0) {
        std::fclose(f);
        return httpd_resp_send(req, "", 0);
    }

    constexpr size_t kChunk = 2048;
    std::vector<char> buf(kChunk);
    long remaining = sz;
    while (remaining > 0) {
        const size_t to_read =
            static_cast<size_t>(std::min<long>(remaining, static_cast<long>(kChunk)));
        const size_t rd = std::fread(buf.data(), 1, to_read, f);
        if (rd != to_read) {
            std::fclose(f);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Read error");
            return ESP_FAIL;
        }
        esp_err_t send_err = httpd_resp_send_chunk(req, buf.data(), rd);
        if (send_err != ESP_OK) {
            std::fclose(f);
            return send_err;
        }
        remaining -= static_cast<long>(rd);
    }
    std::fclose(f);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

#endif // !HOST_TEST_BUILD

namespace http_server {

HttpServer& HttpServer::instance() {
    static HttpServer server;
    return server;
}

common::Result<void> HttpServer::initialize() {
    if (initialized_) {
        return common::Result<void>::error(common::ErrorCode::AlreadyInitialized);
    }
    initialized_ = true;
    state_ = HttpServerState::Stopped;
    return common::Result<void>::ok();
}

#ifndef HOST_TEST_BUILD

bool HttpServer::authorize_request(httpd_req_t* req) {
    size_t len = httpd_req_get_hdr_value_len(req, "Authorization");
    if (len == 0) {
        return false;
    }
    std::vector<char> buf(len + 1);
    if (httpd_req_get_hdr_value_str(req, "Authorization", buf.data(), buf.size()) !=
        ESP_OK) {
        return false;
    }
    const char* p = buf.data();
    static const char kBearer[] = "Bearer ";
    const size_t kBearerLen = sizeof(kBearer) - 1;
    if (strncasecmp(p, kBearer, kBearerLen) != 0) {
        return false;
    }
    const char* token = p + kBearerLen;
    while (*token == ' ' || *token == '\t') {
        ++token;
    }
    return auth_service::AuthService::instance().validate_session(token);
}

common::Result<void> HttpServer::start(uint16_t port) {
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    if (server_) {
        return common::Result<void>::error(common::ErrorCode::AlreadyInitialized);
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = port;
    cfg.max_uri_handlers = 20;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.stack_size = 8192;

    esp_err_t err = httpd_start(&server_, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %d", err);
        server_ = nullptr;
        return common::Result<void>::error(common::ErrorCode::HttpStartFailed);
    }

    port_ = port;
    state_ = HttpServerState::Running;
    ESP_LOGI(TAG, "HTTP server listening on port %u", static_cast<unsigned>(port));
    return common::Result<void>::ok();
}

common::Result<void> HttpServer::register_uri_handler(const httpd_uri_t& uri) {
    if (!server_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    esp_err_t err = httpd_register_uri_handler(server_, &uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register_uri_handler failed: %d", err);
        return common::Result<void>::error(common::ErrorCode::HttpHandlerError);
    }
    return common::Result<void>::ok();
}

common::Result<void> HttpServer::register_static_web_handler() {
    if (!server_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    httpd_uri_t static_uri = {.uri = "/*",
                              .method = HTTP_GET,
                              .handler = static_spiffs_handler,
                              .user_ctx = nullptr};
    return register_uri_handler(static_uri);
}

#endif // !HOST_TEST_BUILD

common::Result<void> HttpServer::stop() {
#ifndef HOST_TEST_BUILD
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    if (server_) {
        esp_err_t err = httpd_stop(server_);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "httpd_stop returned %d", err);
        }
        server_ = nullptr;
    }
    state_ = HttpServerState::Stopped;
    return common::Result<void>::ok();
#else
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    state_ = HttpServerState::Stopped;
    return common::Result<void>::ok();
#endif
}

HttpServerState HttpServer::state() const {
    return state_;
}

#ifdef HOST_TEST_BUILD
common::Result<void> HttpServer::start(uint16_t port) {
    (void)port;
    if (!initialized_) {
        return common::Result<void>::error(common::ErrorCode::NotInitialized);
    }
    state_ = HttpServerState::Running;
    return common::Result<void>::ok();
}
#endif

} // namespace http_server
