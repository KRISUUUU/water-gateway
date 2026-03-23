#pragma once

#include "common/result.hpp"

namespace http_server {

enum class HttpServerState {
    Uninitialized = 0,
    Stopped,
    Running,
    Error
};

class HttpServer {
public:
    static HttpServer& instance();

    common::Result<void> initialize();
    common::Result<void> start();
    common::Result<void> stop();

    [[nodiscard]] HttpServerState state() const;

private:
    HttpServer() = default;

    bool initialized_{false};
    HttpServerState state_{HttpServerState::Uninitialized};
};

}  // namespace http_server
