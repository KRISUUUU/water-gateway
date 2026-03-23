#include "auth_service/auth_service.hpp"

namespace auth_service {

AuthService& AuthService::instance() {
    static AuthService service;
    return service;
}

common::Result<void> AuthService::initialize() {
    if (initialized_) {
        return common::Result<void>(common::ErrorCode::AlreadyInitialized);
    }

    initialized_ = true;
    return common::Result<void>();
}

common::Result<SessionInfo> AuthService::login(const std::string& username, const std::string& password) {
    if (!initialized_) {
        return common::Result<SessionInfo>(common::ErrorCode::NotInitialized);
    }

    if (username.empty() || password.empty()) {
        return common::Result<SessionInfo>(common::ErrorCode::InvalidArgument);
    }

    SessionInfo info{};
    info.authenticated = true;
    info.username = username;
    info.session_token = "placeholder-session-token";
    active_token_ = info.session_token;

    return common::Result<SessionInfo>(info);
}

common::Result<void> AuthService::logout(const std::string& token) {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    if (token != active_token_) {
        return common::Result<void>(common::ErrorCode::AuthFailed);
    }

    active_token_.clear();
    return common::Result<void>();
}

bool AuthService::is_valid_session(const std::string& token) const {
    return !active_token_.empty() && token == active_token_;
}

}  // namespace auth_service
