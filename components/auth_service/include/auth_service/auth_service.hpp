#pragma once

#include <string>

#include "common/result.hpp"

namespace auth_service {

struct SessionInfo {
    bool authenticated{false};
    std::string username{};
    std::string session_token{};
};

class AuthService {
public:
    static AuthService& instance();

    common::Result<void> initialize();
    common::Result<SessionInfo> login(const std::string& username, const std::string& password);
    common::Result<void> logout(const std::string& token);
    [[nodiscard]] bool is_valid_session(const std::string& token) const;

private:
    AuthService() = default;

    bool initialized_{false};
    std::string active_token_{};
};

}  // namespace auth_service
