#pragma once

#include <string>

namespace diagnostics_service {

struct DiagnosticsSnapshot {
    std::string summary_json{};
};

class DiagnosticsService {
public:
    static DiagnosticsService& instance();

    void initialize();
    [[nodiscard]] DiagnosticsSnapshot snapshot() const;

private:
    DiagnosticsService() = default;
};

}  // namespace diagnostics_service
