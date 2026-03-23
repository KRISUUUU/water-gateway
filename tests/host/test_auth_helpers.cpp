#include <cassert>

#include "auth_service/auth_service.hpp"

int main() {
    auto& auth = auth_service::AuthService::instance();

    assert(auth.initialize().ok());

    const auto login = auth.login("admin", "secret");
    assert(login.ok());
    assert(login.value().authenticated);
    assert(auth.is_valid_session(login.value().session_token));

    assert(auth.logout(login.value().session_token).ok());
    assert(!auth.is_valid_session(login.value().session_token));

    return 0;
}
