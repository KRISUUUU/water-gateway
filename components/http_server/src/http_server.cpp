#include "http_server/http_server.hpp"

namespace http_server {

HttpServer& HttpServer::instance() {
    static HttpServer server;
    return server;
}

common::Result<void> HttpServer::initialize() {
    if (initialized_) {
        return common::Result<void>(common::ErrorCode::AlreadyInitialized);
    }

    initialized_ = true;
    state_ = HttpServerState::Stopped;
    return common::Result<void>();
}

common::Result<void> HttpServer::start() {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    state_ = HttpServerState::Running;
    return common::Result<void>();
}

common::Result<void> HttpServer::stop() {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    state_ = HttpServerState::Stopped;
    return common::Result<void>();
}

HttpServerState HttpServer::state() const {
    return state_;
}

}  // namespace http_server
