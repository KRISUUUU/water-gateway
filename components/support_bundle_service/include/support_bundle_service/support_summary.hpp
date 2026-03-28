#pragma once

#include "diagnostics_service/diagnostics_service.hpp"
#include "ota_manager/ota_manager.hpp"

#include <cstdint>
#include <string>

namespace support_bundle_service {

struct SupportSummaryEntry {
    std::string state{};
    std::string message{};
};

struct SupportHealthSummary {
    std::string state{};
    std::string message{};
    std::uint32_t warning_count{0};
    std::uint32_t error_count{0};
};

struct SupportQueueSummary {
    std::string state{};
    std::string message{};
    std::uint32_t outbox_depth{0};
    std::uint32_t outbox_capacity{0};
    bool held_item{false};
    std::uint32_t retry_failure_count{0};
};

struct CompactSupportSummary {
    std::int64_t generated_at_epoch_s{0};
    std::string mode{"unknown"};
    SupportHealthSummary health{};
    SupportSummaryEntry mqtt{};
    SupportQueueSummary queue{};
    SupportSummaryEntry radio{};
    SupportSummaryEntry ota{};
};

SupportHealthSummary summarize_health(const health_monitor::HealthSnapshot& health);
SupportSummaryEntry summarize_mqtt(const mqtt_service::MqttStatus& mqtt);
SupportQueueSummary summarize_queue(const mqtt_service::MqttStatus& mqtt);
SupportSummaryEntry summarize_radio(radio_cc1101::RadioState state,
                                    const radio_cc1101::RadioCounters& counters);
SupportSummaryEntry summarize_ota(const ota_manager::OtaStatus& ota);
CompactSupportSummary build_compact_support_summary(
    const diagnostics_service::DiagnosticsSnapshot& diagnostics,
    const ota_manager::OtaStatus& ota,
    const char* mode,
    std::int64_t generated_at_epoch_s);

} // namespace support_bundle_service
