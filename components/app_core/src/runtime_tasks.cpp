#include "app_core/app_core.hpp"
#include "app_core/radio_rx_wake_model.hpp"

#ifndef HOST_TEST_BUILD

#include "board_config/board_config.hpp"
#include "config_store/config_store.hpp"
#include "event_bus/event_bus.hpp"
#include "health_monitor/health_monitor.hpp"
#include "meter_registry/meter_registry.hpp"
#include "metrics_service/metrics_service.hpp"
#include "mqtt_service/mqtt_publish.hpp"
#include "mqtt_service/mqtt_service.hpp"
#include "ntp_service/ntp_service.hpp"
#include "ota_manager/ota_manager.hpp"
#include "radio_cc1101/radio_cc1101.hpp"
#include "radio_cc1101/cc1101_owner_events.hpp"
#include "radio_state_machine/radio_state_machine.hpp"
#include "protocol_driver/protocol_ids.hpp"
#include "protocol_driver/radio_profile_manager.hpp"
#include "protocol_driver/radio_runtime_plan.hpp"
#include "rf_diagnostics/rf_diagnostics.hpp"
#include "telegram_router/telegram_router.hpp"
#include "watchdog_service/watchdog_service.hpp"
#include "wifi_manager/wifi_manager.hpp"
#include "wmbus_link/wmbus_link.hpp"
#include "wmbus_prios_rx/prios_bringup_session.hpp"
#include "wmbus_prios_rx/prios_capture_service.hpp"
#include "wmbus_prios_rx/prios_decoder.hpp"
#include "wmbus_tmode_rx/rx_session_engine.hpp"

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <ctime>
#include <string>

namespace {

static const char* TAG = "app_runtime";

static QueueHandle_t frame_queue = nullptr;
static QueueHandle_t mqtt_outbox = nullptr;
static TaskHandle_t radio_task_handle = nullptr;
static TaskHandle_t pipeline_task_handle = nullptr;
static TaskHandle_t mqtt_task_handle = nullptr;
static TaskHandle_t health_task_handle = nullptr;
static constexpr uint32_t kFrameQueueDepth = 16;
static constexpr uint32_t kMqttOutboxDepth = 32;
static constexpr uint32_t kCriticalTaskStallMs = 5000;
static constexpr uint32_t kBootValidationStableRuntimeMs = 15000;
static std::atomic<uint32_t> frame_queue_max_depth{0};
static std::atomic<uint32_t> frame_enqueue_success{0};
static std::atomic<uint32_t> frame_enqueue_drop{0};
static std::atomic<uint32_t> frame_enqueue_errors{0};
static std::atomic<uint32_t> frame_queue_send_failures{0};

static std::atomic<uint32_t> mqtt_outbox_peak_depth{0};
static std::atomic<uint32_t> mqtt_outbox_enqueue_success{0};
static std::atomic<uint32_t> mqtt_outbox_enqueue_drop{0};
static std::atomic<uint32_t> mqtt_outbox_enqueue_errors{0};

static std::atomic<uint32_t> radio_loop_last_ms{0};
static std::atomic<uint32_t> pipeline_loop_last_ms{0};
static std::atomic<uint32_t> mqtt_loop_last_ms{0};
static std::atomic<uint32_t> pipeline_frames_processed{0};
static std::atomic<uint32_t> radio_stall_count{0};
static std::atomic<uint32_t> pipeline_stall_count{0};
static std::atomic<uint32_t> mqtt_stall_count{0};
static std::atomic<uint32_t> watchdog_register_errors{0};
static std::atomic<uint32_t> watchdog_feed_errors{0};
static bool boot_valid_marked_ = false;
static std::atomic<bool> pipeline_config_dirty{false};

// Queue item carrying an exact encoded frame together with the protocol
// identity assigned by the radio task. Using a wrapper keeps protocol
// metadata out of wmbus_link types and allows future protocol expansion
// without touching the link-layer structs.
struct RadioRxQueueItem {
    wmbus_link::EncodedRxFrame frame{};
    protocol_driver::ProtocolId      protocol_id     = protocol_driver::ProtocolId::WMbusT;
    protocol_driver::RadioInstanceId radio_instance  = protocol_driver::kRadioInstancePrimary;
    protocol_driver::RadioProfileId  radio_profile   = protocol_driver::RadioProfileId::WMbusT868;
};

static uint32_t now_ms() {
    return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void update_peak(std::atomic<uint32_t>& peak, uint32_t value) {
    uint32_t current = peak.load(std::memory_order_relaxed);
    while (value > current &&
           !peak.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
    }
}

struct RadioRxWaitResult {
    app_core::RadioRxWaitSource source = app_core::RadioRxWaitSource::IdleLivenessTimeout;
    radio_cc1101::RadioOwnerEventSet events{};
};

class RadioSessionDevice final : public wmbus_tmode_rx::SessionRadio {
  public:
    RadioSessionDevice(radio_cc1101::RadioCc1101& radio, void* owner_token)
        : radio_(radio), owner_token_(owner_token) {}

    common::Result<wmbus_tmode_rx::SessionRadioStatus> read_status() override {
        auto status_result = radio_.owner_read_rx_status(owner_token_);
        if (status_result.is_error()) {
            return common::Result<wmbus_tmode_rx::SessionRadioStatus>::error(
                status_result.error());
        }
        const auto& status = status_result.value();
        return common::Result<wmbus_tmode_rx::SessionRadioStatus>::ok(
            {status.fifo_bytes, status.fifo_overflow, status.receiving});
    }

    common::Result<uint16_t> read_fifo(uint8_t* buffer, uint16_t capacity) override {
        return radio_.owner_read_fifo_bytes(owner_token_, buffer, capacity);
    }

    common::Result<wmbus_tmode_rx::SessionSignalQuality> read_signal_quality() override {
        auto quality_result = radio_.owner_read_signal_quality(owner_token_);
        if (quality_result.is_error()) {
            return common::Result<wmbus_tmode_rx::SessionSignalQuality>::error(
                quality_result.error());
        }
        const auto& quality = quality_result.value();
        return common::Result<wmbus_tmode_rx::SessionSignalQuality>::ok(
            {quality.rssi_dbm, quality.lqi, quality.crc_ok, quality.radio_crc_available});
    }

    common::Result<void> switch_to_fixed_length(uint8_t remaining_encoded_bytes) override {
        return radio_.owner_switch_to_fixed_length_capture(owner_token_, remaining_encoded_bytes);
    }

    common::Result<void> restore_infinite_packet_mode() override {
        return radio_.owner_restore_infinite_packet_mode(owner_token_);
    }

    common::Result<void> abort_and_restart_rx() override {
        return radio_.owner_abort_and_restart_rx(owner_token_);
    }

  private:
    radio_cc1101::RadioCc1101& radio_;
    void* owner_token_ = nullptr;
};

static wmbus_link::EncodedRxFrame encode_session_capture(
    const wmbus_tmode_rx::SessionFrameCapture& capture, int64_t timestamp_ms, uint32_t rx_count) {
    wmbus_link::EncodedRxFrame frame{};
    frame.encoded_length = capture.candidate.encoded_length;
    frame.decoded_length = capture.candidate.decoded_length;
    frame.exact_encoded_bytes_required = capture.candidate.exact_encoded_bytes_required;
    frame.l_field = capture.candidate.l_field;
    frame.orientation = capture.candidate.orientation;
    frame.first_block_validation = capture.candidate.first_block_validation;
    std::memcpy(frame.encoded_bytes.data(), capture.candidate.encoded_bytes.data(),
                capture.candidate.encoded_length);
    std::memcpy(frame.decoded_bytes.data(), capture.candidate.decoded_bytes.data(),
                capture.candidate.decoded_length);
    frame.metadata.rssi_dbm = capture.rssi_dbm;
    frame.metadata.lqi = capture.lqi;
    frame.metadata.crc_ok = capture.crc_ok;
    frame.metadata.radio_crc_available = capture.radio_crc_available;
    frame.metadata.exact_frame_contract_valid = true;
    frame.metadata.transitional_raw_adapter_used = false;
    frame.metadata.timestamp_ms = timestamp_ms;
    frame.metadata.rx_count = rx_count;
    frame.metadata.capture_elapsed_ms = capture.capture_elapsed_ms;
    frame.metadata.captured_frame_length = capture.candidate.encoded_length;
    frame.metadata.first_data_byte = capture.first_data_byte;

    return frame;
}

static void populate_rf_diagnostic_timestamps(rf_diagnostics::RfDiagnosticRecord& record) {
    auto& ntp = ntp_service::NtpService::instance();
    const auto ts = ntp.now_epoch_ms();
    record.timestamp_epoch_ms = ts > 0 ? ts : 0;
    record.monotonic_ms = ntp.monotonic_now_ms();
    record.sequence = rf_diagnostics::RfDiagnosticsService::instance().snapshot().total_inserted + 1U;
}

struct RadioStartupConfigSnapshot {
    protocol_driver::RadioRuntimePlan runtime_plan =
        protocol_driver::RadioRuntimePlan::single_radio(
            protocol_driver::RadioSchedulerMode::Locked,
            protocol_driver::kRadioProfileMaskWMbusT868);
    bool campaign_active = false;
    bool discovery_active = false;
    bool campaign_manchester = false;
};

static RadioStartupConfigSnapshot load_radio_startup_config() {
    const auto cfg = config_store::ConfigStore::instance().config();
    return {
        .runtime_plan = protocol_driver::RadioRuntimePlan::single_radio(
            cfg.radio.scheduler_mode, cfg.radio.enabled_profiles),
        .campaign_active = cfg.radio.prios_capture_campaign,
        .discovery_active = cfg.radio.prios_discovery_mode,
        .campaign_manchester = cfg.radio.prios_manchester_enabled,
    };
}

struct RuntimeProfileApplyResult {
    protocol_driver::RadioProfileId requested_profile =
        protocol_driver::RadioProfileId::Unknown;
    protocol_driver::RadioProfileId applied_profile =
        protocol_driver::RadioProfileId::Unknown;
    protocol_driver::ProfileApplyStatus apply_status =
        protocol_driver::ProfileApplyStatus::Pending;
    common::ErrorCode error = common::ErrorCode::OK;
};

static const char* prios_mode_to_string(wmbus_prios_rx::PriosBringUpSession::Mode mode) {
    return mode == wmbus_prios_rx::PriosBringUpSession::Mode::DiscoverySniffer
               ? "DiscoverySniffer"
               : "SyncCampaign";
}

static RuntimeProfileApplyResult apply_runtime_radio_profile(
    radio_cc1101::RadioCc1101& radio, void* owner_token,
    protocol_driver::RadioProfileId requested_profile, bool experimental_prios_active,
    bool discovery_active, bool manchester_enabled,
    wmbus_tmode_rx::RxSessionEngine& session_engine,
    wmbus_prios_rx::PriosBringUpSession& prios_session) {
    RuntimeProfileApplyResult result{};
    result.requested_profile = requested_profile;

    switch (requested_profile) {
        case protocol_driver::RadioProfileId::WMbusT868: {
            session_engine.reset();
            prios_session.reset();
            auto apply = radio.owner_apply_tmode_profile(owner_token);
            if (apply.is_error()) {
                result.apply_status = protocol_driver::ProfileApplyStatus::ApplyFailed;
                result.error = apply.error();
                return result;
            }
            result.applied_profile = protocol_driver::RadioProfileId::WMbusT868;
            result.apply_status = protocol_driver::ProfileApplyStatus::Applied;
            return result;
        }

        case protocol_driver::RadioProfileId::WMbusPriosR3: {
            const auto mode =
                (experimental_prios_active && !discovery_active)
                    ? wmbus_prios_rx::PriosBringUpSession::Mode::SyncCampaign
                    : wmbus_prios_rx::PriosBringUpSession::Mode::DiscoverySniffer;
            session_engine.reset();
            prios_session.reset();
            prios_session.configure(mode, manchester_enabled);
            auto apply =
                mode == wmbus_prios_rx::PriosBringUpSession::Mode::SyncCampaign
                    ? radio.owner_apply_prios_r3_profile(owner_token, manchester_enabled)
                    : radio.owner_apply_prios_r3_discovery_profile(owner_token, manchester_enabled);
            if (apply.is_error()) {
                result.apply_status = protocol_driver::ProfileApplyStatus::ApplyFailed;
                result.error = apply.error();
                return result;
            }
            result.applied_profile = protocol_driver::RadioProfileId::WMbusPriosR3;
            result.apply_status = protocol_driver::ProfileApplyStatus::Applied;
            return result;
        }

        case protocol_driver::RadioProfileId::WMbusPriosR4: {
            session_engine.reset();
            prios_session.reset();
            auto fallback = radio.owner_apply_tmode_profile(owner_token);
            if (fallback.is_error()) {
                result.apply_status = protocol_driver::ProfileApplyStatus::ApplyFailed;
                result.error = fallback.error();
                return result;
            }
            result.applied_profile = protocol_driver::RadioProfileId::WMbusT868;
            result.apply_status = protocol_driver::ProfileApplyStatus::UnsupportedRequestedProfile;
            return result;
        }

        default:
            result.apply_status = protocol_driver::ProfileApplyStatus::ApplyFailed;
            result.error = common::ErrorCode::InvalidArgument;
            return result;
    }
}

static void log_runtime_profile_apply(const RuntimeProfileApplyResult& result,
                                      protocol_driver::SchedulerSwitchReason switch_reason,
                                      bool manchester_enabled,
                                      wmbus_prios_rx::PriosBringUpSession::Mode prios_mode) {
    if (result.error != common::ErrorCode::OK) {
        ESP_LOGW(TAG,
                 "radio profile apply failed: requested=%s reason=%s error=%s",
                 protocol_driver::radio_profile_id_to_string(result.requested_profile),
                 protocol_driver::scheduler_switch_reason_to_string(switch_reason),
                 common::error_code_to_string(result.error));
        return;
    }

    ESP_LOGI(TAG,
             "radio profile apply: requested=%s applied=%s status=%s reason=%s variant=%s prios_mode=%s",
             protocol_driver::radio_profile_id_to_string(result.requested_profile),
             protocol_driver::radio_profile_id_to_string(result.applied_profile),
             protocol_driver::profile_apply_status_to_string(result.apply_status),
             protocol_driver::scheduler_switch_reason_to_string(switch_reason),
             manchester_enabled ? "manchester_on" : "manchester_off",
             prios_mode_to_string(prios_mode));
}

static void log_radio_boot_stack_headroom(TaskHandle_t radio_task) {
    if (!radio_task) {
        return;
    }
    const auto hwm_words = static_cast<unsigned long>(uxTaskGetStackHighWaterMark(radio_task));
    ESP_LOGI(TAG, "radio_rx boot stack_hwm=%lu words", hwm_words);
}

static RadioRxWaitResult wait_for_radio_rx_work(
    radio_cc1101::RadioCc1101& radio, bool irq_plumbing_enabled, bool session_active,
    uint32_t session_watchdog_tick_ms) {
    const auto policy = app_core::make_radio_rx_wake_policy(irq_plumbing_enabled, session_active,
                                                            session_watchdog_tick_ms);
    const uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(policy.wait_timeout_ms));
    if (notified == 0U) {
        return {policy.timeout_source, policy.timeout_events};
    }
    return {app_core::RadioRxWaitSource::IrqNotification, radio.take_owner_events()};
}

static void sample_queue_levels() {
    uint32_t frame_depth = 0;
    uint32_t outbox_depth = 0;
    if (frame_queue) {
        frame_depth = static_cast<uint32_t>(uxQueueMessagesWaiting(frame_queue));
        update_peak(frame_queue_max_depth, frame_depth);
    }
    if (mqtt_outbox) {
        outbox_depth = static_cast<uint32_t>(uxQueueMessagesWaiting(mqtt_outbox));
        update_peak(mqtt_outbox_peak_depth, outbox_depth);
        mqtt_service::MqttService::instance().report_outbox_depth(outbox_depth);
    }
    metrics_service::MetricsService::report_queue_metrics(
        frame_depth, frame_queue_max_depth.load(std::memory_order_relaxed),
        frame_queue_max_depth.load(std::memory_order_relaxed),
        frame_enqueue_success.load(std::memory_order_relaxed),
        frame_enqueue_drop.load(std::memory_order_relaxed),
        frame_enqueue_errors.load(std::memory_order_relaxed),
        frame_queue_send_failures.load(std::memory_order_relaxed), outbox_depth,
        mqtt_outbox_peak_depth.load(std::memory_order_relaxed),
        mqtt_outbox_enqueue_success.load(std::memory_order_relaxed),
        mqtt_outbox_enqueue_drop.load(std::memory_order_relaxed),
        mqtt_outbox_enqueue_errors.load(std::memory_order_relaxed));

    const uint32_t now = now_ms();
    const uint32_t radio_age = now - radio_loop_last_ms.load(std::memory_order_relaxed);
    const uint32_t pipeline_age = now - pipeline_loop_last_ms.load(std::memory_order_relaxed);
    const uint32_t mqtt_age = now - mqtt_loop_last_ms.load(std::memory_order_relaxed);
    metrics_service::MetricsService::report_task_metrics(
        radio_age, pipeline_age, mqtt_age,
        pipeline_frames_processed.load(std::memory_order_relaxed),
        radio_stall_count.load(std::memory_order_relaxed),
        pipeline_stall_count.load(std::memory_order_relaxed),
        mqtt_stall_count.load(std::memory_order_relaxed),
        watchdog_register_errors.load(std::memory_order_relaxed),
        watchdog_feed_errors.load(std::memory_order_relaxed));
}

static void sample_task_stack_watermarks() {
    uint32_t radio_hwm = 0;
    uint32_t pipeline_hwm = 0;
    uint32_t mqtt_hwm = 0;
    uint32_t health_hwm = 0;
    if (radio_task_handle) {
        radio_hwm = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(radio_task_handle));
    }
    if (pipeline_task_handle) {
        pipeline_hwm = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(pipeline_task_handle));
    }
    if (mqtt_task_handle) {
        mqtt_hwm = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(mqtt_task_handle));
    }
    if (health_task_handle) {
        health_hwm = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(health_task_handle));
    }
    metrics_service::MetricsService::report_task_stack_metrics(radio_hwm, pipeline_hwm, mqtt_hwm,
                                                               health_hwm);
}

static void cleanup_runtime_resources() {
    auto& radio = radio_cc1101::RadioCc1101::instance();
    if (radio_task_handle) {
        radio.release_owner(radio_task_handle);
    } else {
        radio.disable_gdo_interrupts();
    }
    if (health_task_handle) {
        vTaskDelete(health_task_handle);
        health_task_handle = nullptr;
    }
    if (mqtt_task_handle) {
        vTaskDelete(mqtt_task_handle);
        mqtt_task_handle = nullptr;
    }
    if (pipeline_task_handle) {
        vTaskDelete(pipeline_task_handle);
        pipeline_task_handle = nullptr;
    }
    if (radio_task_handle) {
        vTaskDelete(radio_task_handle);
        radio_task_handle = nullptr;
    }
    if (mqtt_outbox) {
        vQueueDelete(mqtt_outbox);
        mqtt_outbox = nullptr;
    }
    if (frame_queue) {
        vQueueDelete(frame_queue);
        frame_queue = nullptr;
    }
}

static bool enqueue_mqtt(const mqtt_service::MqttPublishCommand& command) {
    auto& mqtt = mqtt_service::MqttService::instance();
    if (!mqtt_outbox) {
        mqtt.report_outbox_enqueue_failure(false);
        mqtt_outbox_enqueue_errors.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (xQueueSend(mqtt_outbox, &command, pdMS_TO_TICKS(10)) == pdTRUE) {
        mqtt_outbox_enqueue_success.fetch_add(1, std::memory_order_relaxed);
        sample_queue_levels();
        return true;
    }

    const uint32_t dropped = mqtt_outbox_enqueue_drop.fetch_add(1, std::memory_order_relaxed) + 1;
    mqtt_outbox_enqueue_errors.fetch_add(1, std::memory_order_relaxed);
    mqtt.report_outbox_enqueue_failure(false);
    if ((dropped % 32U) == 1U) {
        ESP_LOGW(TAG, "MQTT command enqueue failed (drops=%lu depth=%lu/%lu type=%d)",
                 static_cast<unsigned long>(dropped),
                 static_cast<unsigned long>(uxQueueMessagesWaiting(mqtt_outbox)),
                 static_cast<unsigned long>(kMqttOutboxDepth), static_cast<int>(command.type));
    }
    sample_queue_levels();
    return false;
}

static std::string derive_meter_key(const wmbus_link::ValidatedTelegram& telegram) {
    return telegram.identity_key();
}

static void format_timestamp(int64_t ts, bool ntp_synced,
                             char (&out)[mqtt_service::kPublishTimestampCapacity]) {
    std::strncpy(out, "timestamp_unavailable", sizeof(out) - 1U);
    out[sizeof(out) - 1U] = '\0';
    if (ntp_synced && ts > 0) {
        const time_t sec = static_cast<time_t>(ts / 1000);
        struct tm t;
        gmtime_r(&sec, &t);
        strftime(out, sizeof(out), "%Y-%m-%dT%H:%M:%SZ", &t);
    } else if (ts > 0) {
        std::snprintf(out, sizeof(out), "monotonic_ms:%lld", static_cast<long long>(ts));
    }
}

static void radio_rx_task(void* /*param*/) {
    auto& radio = radio_cc1101::RadioCc1101::instance();
    auto& rsm = radio_state_machine::RadioStateMachine::instance();
    auto& wd = watchdog_service::WatchdogService::instance();
    auto& profile_mgr = protocol_driver::RadioProfileManager::instance();
    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    RadioSessionDevice session_device(radio, current_task);
    const wmbus_tmode_rx::SessionEngineConfig session_config{};
    wmbus_tmode_rx::RxSessionEngine session_engine(session_config);
    // PRIOS bring-up session: capture-only, no decode.
    wmbus_prios_rx::PriosBringUpSession prios_session{};
    uint32_t rx_count = 0;
    bool irq_plumbing_enabled = false;
    const uint32_t session_watchdog_tick_ms =
        std::min(session_config.idle_poll_timeout_ms, session_config.min_watchdog_timeout_ms);
    constexpr protocol_driver::ProtocolId kTmodeProtocolId = protocol_driver::ProtocolId::WMbusT;
    constexpr protocol_driver::RadioProfileId kTmodeRadioProfile =
        protocol_driver::RadioProfileId::WMbusT868;

    // Read only the radio fields required at startup. The full AppConfig is
    // large, so keep that copy scoped to the helper call instead of the
    // radio_rx task lifetime.
    const auto startup_radio_cfg = load_radio_startup_config();
    const auto* const primary_runtime_cfg =
        startup_radio_cfg.runtime_plan.instance(protocol_driver::kRadioInstancePrimary);
    const bool campaign_active = startup_radio_cfg.campaign_active;
    const bool discovery_active = startup_radio_cfg.discovery_active;
    const bool campaign_manchester = startup_radio_cfg.campaign_manchester;
    const bool experimental_prios_active = campaign_active || discovery_active;

    if (experimental_prios_active) {
        // Experimental PRIOS modes override the user's scheduler config and lock
        // the primary radio to WMbusPriosR3 for the duration of the session.
        // T-mode reception is suspended.  PriosExperimentalLock is recorded as
        // the switch reason so the diagnostics API makes the override visible.
        profile_mgr.configure(protocol_driver::RadioSchedulerMode::Locked,
                              protocol_driver::kRadioProfileMaskWMbusPriosR3,
                              protocol_driver::kRadioInstancePrimary,
                              protocol_driver::SchedulerSwitchReason::PriosExperimentalLock);
        ESP_LOGI(TAG, "PRIOS %s ACTIVE — radio locked to WMbusPriosR3 "
                      "(variant=%s, T-mode suspended, reason=PriosExperimentalLock)",
                 discovery_active ? "discovery/sniffer mode"
                                  : "capture campaign",
                 campaign_manchester ? "manchester_on" : "manchester_off");
    } else {
        // Normal operation: apply scheduler mode and enabled profiles from config.
        profile_mgr.configure(primary_runtime_cfg ? primary_runtime_cfg->scheduler_mode
                                                  : protocol_driver::RadioSchedulerMode::Locked,
                              primary_runtime_cfg ? primary_runtime_cfg->enabled_profiles
                                                  : protocol_driver::kRadioProfileMaskWMbusT868,
                              protocol_driver::kRadioInstancePrimary,
                              protocol_driver::SchedulerSwitchReason::Initial);
    }

    auto owner_claim = radio.claim_owner(current_task);
    if (owner_claim.is_error()) {
        ESP_LOGE(TAG, "radio_rx failed to claim sole radio ownership");
        vTaskDelete(nullptr);
        return;
    }

    {
        const auto apply_result = apply_runtime_radio_profile(
            radio, current_task, profile_mgr.selected_profile_id(), experimental_prios_active,
            discovery_active, campaign_manchester, session_engine, prios_session);
        if (apply_result.error == common::ErrorCode::OK) {
            profile_mgr.note_profile_applied(apply_result.applied_profile,
                                             apply_result.apply_status);
        } else {
            profile_mgr.note_profile_applied(profile_mgr.active_profile_id(),
                                             protocol_driver::ProfileApplyStatus::ApplyFailed);
        }
        log_runtime_profile_apply(apply_result, profile_mgr.status().last_switch_reason,
                                  campaign_manchester, prios_session.mode());
    }

    auto irq_enable = radio.enable_gdo_interrupts(current_task, current_task);
    if (irq_enable.is_error()) {
        // The active RX path is IRQ-first. If owner-task IRQ plumbing is unavailable on a board
        // build, keep the same RX path but fall back to bounded polling for liveness.
        ESP_LOGW(TAG, "radio_rx IRQ wake plumbing unavailable, enabling bounded poll fallback (%d)",
                 static_cast<int>(irq_enable.error()));
    } else {
        irq_plumbing_enabled = true;
    }

    if (wd.register_task().is_error()) {
        watchdog_register_errors.fetch_add(1, std::memory_order_relaxed);
        ESP_LOGW(TAG, "watchdog register failed for radio_rx");
    }
    log_radio_boot_stack_headroom(current_task);

    while (true) {
        radio_loop_last_ms.store(now_ms(), std::memory_order_relaxed);
        rsm.tick();
        const bool session_active = session_engine.snapshot().active;
        const auto wait_result = wait_for_radio_rx_work(radio, irq_plumbing_enabled, session_active,
                                                        session_watchdog_tick_ms);
        const auto& owner_events = wait_result.events;

        if (wait_result.source == app_core::RadioRxWaitSource::IrqNotification) {
            profile_mgr.record_irq_wake();
        } else {
            profile_mgr.record_fallback_wake();
        }

        const auto active_profile = profile_mgr.active_profile_id();
        const auto selected_profile_before = profile_mgr.selected_profile_id();

        // PRIOS bring-up path: raw bounded capture, no decode, no link-layer routing.
        if (active_profile == protocol_driver::RadioProfileId::WMbusPriosR3) {
            const int64_t ts = ntp_service::NtpService::instance().now_epoch_ms();
            const auto prios_result = prios_session.process(
                session_device, owner_events, now_ms(), ts > 0 ? ts : 0);
            if (prios_result.has_capture) {
                const auto dedup_result =
                    wmbus_prios_rx::PriosCaptureService::instance().insert_with_dedup_gate(
                        prios_result.record);
                if (dedup_result ==
                    wmbus_prios_rx::PriosCaptureInsertDecision::RejectedNewDeviceLimit) {
                    ESP_LOGD(TAG,
                             "device limit reached (kMaxTrackedDevices=%zu), new fingerprint ignored",
                             wmbus_prios_rx::PriosCaptureService::kMaxTrackedDevices);
                } else if (dedup_result ==
                           wmbus_prios_rx::PriosCaptureInsertDecision::Inserted ||
                           dedup_result ==
                           wmbus_prios_rx::PriosCaptureInsertDecision::RejectedDuplicate) {
                    const auto decoded =
                        wmbus_prios_rx::PriosDecoder::decode(prios_result.record);
                    if (decoded.valid) {
                        meter_registry::MeterRegistry::instance().observe_prios_telegram(decoded);
                        const auto& cfg = config_store::ConfigStore::instance().config();
                        const int64_t ts_ms = prios_result.record.timestamp_ms;
                        const bool ntp_synced =
                            ntp_service::NtpService::instance().status().synchronized;
                        char ts_str[mqtt_service::kPublishTimestampCapacity]{};
                        format_timestamp(ts_ms, ntp_synced, ts_str);
                        auto cmd = mqtt_service::make_prios_frame_command(
                            cfg.mqtt.prefix, cfg.device.hostname,
                            decoded.meter_key, decoded.display_prefix_hex,
                            decoded.captured_length, decoded.rssi_dbm, decoded.lqi,
                            decoded.manchester_enabled, ts_str, decoded.manufacturer, decoded.encrypted);
                        if (cmd.is_ok()) {
                            enqueue_mqtt(cmd.value());
                        }
                    }
                }
            }
            if (prios_result.radio_error != common::ErrorCode::OK) {
                rsm.on_read_failure(prios_result.radio_error);
            }
            if (wd.feed().is_error()) {
                const uint32_t errors =
                    watchdog_feed_errors.fetch_add(1, std::memory_order_relaxed) + 1U;
                if ((errors % 64U) == 1U) {
                    ESP_LOGW(TAG, "watchdog feed failed in radio_rx prios path (errors=%lu)",
                             static_cast<unsigned long>(errors));
                }
            }
            if (!experimental_prios_active &&
                wait_result.source != app_core::RadioRxWaitSource::IrqNotification &&
                !prios_session.active()) {
                const auto next_selected = profile_mgr.advance();
                if (next_selected != selected_profile_before) {
                    const auto apply_result = apply_runtime_radio_profile(
                        radio, current_task, next_selected, false, false,
                        campaign_manchester, session_engine, prios_session);
                    if (apply_result.error == common::ErrorCode::OK) {
                        profile_mgr.note_profile_applied(apply_result.applied_profile,
                                                         apply_result.apply_status);
                    } else {
                        profile_mgr.note_profile_applied(active_profile,
                                                         protocol_driver::ProfileApplyStatus::ApplyFailed);
                    }
                    log_runtime_profile_apply(apply_result,
                                              profile_mgr.status().last_switch_reason,
                                              campaign_manchester, prios_session.mode());
                }
            }
            continue;
        }

        auto step_result = session_engine.process(session_device, owner_events, now_ms());
        if (step_result.is_ok()) {
            const auto& step = step_result.value();
            if (step.has_frame && frame_queue) {
                const char* radio_crc_state = "unavailable";
                if (step.frame.radio_crc_available) {
                    radio_crc_state = step.frame.crc_ok ? "ok" : "fail";
                }
                ESP_LOGI(TAG, "Exact-frame compiled: %u bytes, radio_crc=%s",
                         step.frame.candidate.decoded_length, radio_crc_state);
                metrics_service::MetricsService::report_session_completed(
                    step.frame.radio_crc_available, step.frame.crc_ok);
                rsm.on_read_success();
                rx_count++;
                RadioRxQueueItem item{};
                item.frame = encode_session_capture(step.frame,
                                                    ntp_service::NtpService::instance().now_epoch_ms(),
                                                    rx_count);
                item.protocol_id    = kTmodeProtocolId;
                item.radio_instance = protocol_driver::kRadioInstancePrimary;
                item.radio_profile  = kTmodeRadioProfile;
                if (xQueueSend(frame_queue, &item, pdMS_TO_TICKS(10)) == pdTRUE) {
                    frame_enqueue_success.fetch_add(1, std::memory_order_relaxed);
                    sample_queue_levels();
                } else {
                    const uint32_t dropped =
                        frame_enqueue_drop.fetch_add(1, std::memory_order_relaxed) + 1U;
                    frame_enqueue_errors.fetch_add(1, std::memory_order_relaxed);
                    frame_queue_send_failures.fetch_add(1, std::memory_order_relaxed);
                    if ((dropped % 32U) == 1U) {
                        ESP_LOGW(TAG, "Frame queue full/drop (drops=%lu depth=%lu/%lu)",
                                 static_cast<unsigned long>(dropped),
                                 static_cast<unsigned long>(uxQueueMessagesWaiting(frame_queue)),
                                 static_cast<unsigned long>(kFrameQueueDepth));
                    }
                    sample_queue_levels();
                }
            } else if (step.has_diagnostic) {
                ESP_LOGI(TAG, "Session aborted: reject_reason=%d, radio_error=%s",
                         static_cast<int>(step.diagnostic.reject_reason),
                         common::error_code_to_string(step.radio_error));
                metrics_service::MetricsService::report_session_aborted();
                auto record = step.diagnostic;
                populate_rf_diagnostic_timestamps(record);
                rf_diagnostics::RfDiagnosticsService::instance().insert(record);
            }

            if (step.radio_error != common::ErrorCode::OK) {
                rsm.on_read_failure(step.radio_error);
            }
        } else {
            rsm.on_read_failure(step_result.error());
        }

        if (wd.feed().is_error()) {
            const uint32_t errors =
                watchdog_feed_errors.fetch_add(1, std::memory_order_relaxed) + 1U;
            if ((errors % 64U) == 1U) {
                ESP_LOGW(TAG, "watchdog feed failed in radio_rx (errors=%lu)",
                         static_cast<unsigned long>(errors));
            }
        }

        if (!experimental_prios_active &&
            wait_result.source != app_core::RadioRxWaitSource::IrqNotification &&
            !session_engine.snapshot().active) {
            const auto next_selected = profile_mgr.advance();
            if (next_selected != selected_profile_before) {
                const auto apply_result = apply_runtime_radio_profile(
                    radio, current_task, next_selected, false, false,
                    campaign_manchester, session_engine, prios_session);
                if (apply_result.error == common::ErrorCode::OK) {
                    profile_mgr.note_profile_applied(apply_result.applied_profile,
                                                     apply_result.apply_status);
                } else {
                    profile_mgr.note_profile_applied(active_profile,
                                                     protocol_driver::ProfileApplyStatus::ApplyFailed);
                }
                log_runtime_profile_apply(apply_result, profile_mgr.status().last_switch_reason,
                                          campaign_manchester, prios_session.mode());
            }
        }
    }
}

static void pipeline_task(void* /*param*/) {
    auto& router = telegram_router::TelegramRouter::instance();
    auto& wd = watchdog_service::WatchdogService::instance();
    config_store::AppConfig cached_cfg = config_store::ConfigStore::instance().config();
    auto cfg_sub = event_bus::EventBus::instance().subscribe(
        event_bus::EventType::ConfigChanged,
        [](const event_bus::Event&) { pipeline_config_dirty.store(true, std::memory_order_relaxed); });
    if (cfg_sub.is_error()) {
        ESP_LOGW(TAG, "pipeline failed to subscribe config cache invalidation");
    }

    if (wd.register_task().is_error()) {
        watchdog_register_errors.fetch_add(1, std::memory_order_relaxed);
        ESP_LOGW(TAG, "watchdog register failed for pipeline");
    }

    RadioRxQueueItem rx_item{};
    while (true) {
        pipeline_loop_last_ms.store(now_ms(), std::memory_order_relaxed);
        if (pipeline_config_dirty.exchange(false, std::memory_order_relaxed)) {
            cached_cfg = config_store::ConfigStore::instance().config();
        }
        if (frame_queue && xQueueReceive(frame_queue, &rx_item, pdMS_TO_TICKS(100)) == pdTRUE) {
            sample_queue_levels();
            pipeline_frames_processed.fetch_add(1, std::memory_order_relaxed);

            wmbus_link::EncodedRxFrame& exact_frame = rx_item.frame;

            auto& ntp = ntp_service::NtpService::instance();
            const bool ntp_synced = ntp.status().synchronized;
            int64_t ts = ntp.now_epoch_ms();
            if (ts <= 0) {
                ts = ntp.monotonic_now_ms();
            }

            if (exact_frame.metadata.timestamp_ms == 0) {
                exact_frame.metadata.timestamp_ms = ts;
            }
            const auto link_result = wmbus_link::WmbusLink::validate_and_build(exact_frame);
            if (!link_result.accepted) {
                rf_diagnostics::RfDiagnosticRecord record{};
                populate_rf_diagnostic_timestamps(record);
                record.timestamp_epoch_ms = ntp_synced ? ts : 0;
                record.reject_reason =
                    wmbus_link::link_reject_to_rf_reason(link_result.reject_reason);
                record.orientation =
                    exact_frame.orientation == wmbus_tmode_rx::FrameOrientation::BitReversed
                        ? rf_diagnostics::Orientation::BitReversed
                        : (exact_frame.orientation == wmbus_tmode_rx::FrameOrientation::Normal
                               ? rf_diagnostics::Orientation::Normal
                               : rf_diagnostics::Orientation::Unknown);
                record.expected_encoded_length = exact_frame.exact_encoded_bytes_required;
                record.actual_encoded_length = exact_frame.encoded_length;
                record.expected_decoded_length = exact_frame.decoded_length;
                record.actual_decoded_length = exact_frame.decoded_length;
                record.capture_elapsed_ms = exact_frame.metadata.capture_elapsed_ms;
                record.first_data_byte = exact_frame.metadata.first_data_byte;
                record.signal_quality_valid = true;
                record.rssi_dbm = exact_frame.metadata.rssi_dbm;
                record.lqi = exact_frame.metadata.lqi;
                record.radio_crc_available = exact_frame.metadata.radio_crc_available;
                record.radio_crc_ok = exact_frame.metadata.crc_ok;
                record.captured_prefix_length =
                    std::min<size_t>(exact_frame.encoded_length, record.captured_prefix.size());
                for (size_t i = 0; i < record.captured_prefix_length; ++i) {
                    record.captured_prefix[i] = exact_frame.encoded_bytes[i];
                }
                record.decoded_prefix_length =
                    std::min<size_t>(exact_frame.decoded_length, record.decoded_prefix.size());
                for (size_t i = 0; i < record.decoded_prefix_length; ++i) {
                    record.decoded_prefix[i] = exact_frame.decoded_bytes[i];
                }
                rf_diagnostics::RfDiagnosticsService::instance().insert(record);
                metrics_service::MetricsService::report_telegram_link_rejected();
                continue;
            }

            const auto& telegram = link_result.telegram;
            metrics_service::MetricsService::report_telegram_validated();
            const auto route = router.route(telegram);
            const bool duplicate =
                route.decision == telegram_router::RouteDecision::SuppressDuplicate;
            meter_registry::MeterRegistry::instance().observe_telegram(telegram, duplicate);

            if (route.publish_raw) {
                const auto& cfg = cached_cfg;
                char ts_str[mqtt_service::kPublishTimestampCapacity]{};
                format_timestamp(ts, ntp_synced, ts_str);
                auto command_result = mqtt_service::make_raw_frame_command(
                    cfg.mqtt.prefix, cfg.device.hostname, telegram.link.canonical_bytes.data(),
                    telegram.link.metadata.canonical_length, telegram.link.metadata.rssi_dbm,
                    telegram.link.metadata.lqi, telegram.link.metadata.crc_ok,
                    telegram.link.metadata.radio_crc_available, telegram.manufacturer_id(),
                    telegram.device_id(), derive_meter_key(telegram).c_str(), ts_str,
                    telegram.link.metadata.rx_count, true,
                    telegram.exact_frame.metadata.exact_frame_contract_valid);
                if (command_result.is_ok()) {
                    enqueue_mqtt(command_result.value());
                } else {
                    mqtt_service::MqttService::instance().report_outbox_enqueue_failure(true);
                }
            }

            if (route.publish_event && route.event_message) {
                const auto& cfg = cached_cfg;
                auto command_result = mqtt_service::make_event_command(
                    cfg.mqtt.prefix, cfg.device.hostname, "radio_event", "warning",
                    route.event_message, "");
                if (command_result.is_ok()) {
                    enqueue_mqtt(command_result.value());
                } else {
                    mqtt_service::MqttService::instance().report_outbox_enqueue_failure(true);
                }
            }
        }
        if (wd.feed().is_error()) {
            const uint32_t errors =
                watchdog_feed_errors.fetch_add(1, std::memory_order_relaxed) + 1U;
            if ((errors % 64U) == 1U) {
                ESP_LOGW(TAG, "watchdog feed failed in pipeline (errors=%lu)",
                         static_cast<unsigned long>(errors));
            }
        }
    }
}

static void mqtt_task(void* /*param*/) {
    auto& mqtt = mqtt_service::MqttService::instance();
    auto& wd = watchdog_service::WatchdogService::instance();
    mqtt_service::MqttPublishCommand item{};
    mqtt_service::MqttPublishCommand carry{};
    bool carry_pending = false;
    uint32_t publish_retry_failures = 0;

    if (wd.register_task().is_error()) {
        watchdog_register_errors.fetch_add(1, std::memory_order_relaxed);
        ESP_LOGW(TAG, "watchdog register failed for mqtt_pub");
    }

    while (true) {
        mqtt_loop_last_ms.store(now_ms(), std::memory_order_relaxed);
        if (carry_pending) {
            if (mqtt.is_connected()) {
                auto publish_result = mqtt.publish(carry, 0, false);
                if (publish_result.is_ok()) {
                    carry_pending = false;
                    publish_retry_failures = 0;
                    mqtt.report_outbox_carry_pending(false);
                } else {
                    publish_retry_failures++;
                    mqtt.report_outbox_carry_retry_attempt();
                    if ((publish_retry_failures % 32U) == 1U) {
                        ESP_LOGW(TAG, "MQTT publish retry failed (failures=%lu type=%d err=%d)",
                                 static_cast<unsigned long>(publish_retry_failures),
                                 static_cast<int>(carry.type), static_cast<int>(publish_result.error()));
                    }
                }
            } else {
                mqtt.report_outbox_carry_retry_attempt();
            }
        } else if (mqtt_outbox && xQueueReceive(mqtt_outbox, &item, pdMS_TO_TICKS(500)) == pdTRUE) {
            sample_queue_levels();
            if (mqtt.is_connected()) {
                auto publish_result = mqtt.publish(item, 0, false);
                if (publish_result.is_error()) {
                    std::memcpy(&carry, &item, sizeof(carry));
                    carry_pending = true;
                    publish_retry_failures = 1;
                    mqtt.report_outbox_carry_pending(true);
                    mqtt.report_outbox_carry_retry_attempt();
                    ESP_LOGW(TAG, "MQTT publish failed, staging carry type=%d err=%d",
                             static_cast<int>(item.type), static_cast<int>(publish_result.error()));
                }
            } else {
                std::memcpy(&carry, &item, sizeof(carry));
                carry_pending = true;
                publish_retry_failures = 0;
                mqtt.report_outbox_carry_pending(true);
                mqtt.report_outbox_carry_retry_attempt();
            }
        } else {
            mqtt.report_outbox_carry_pending(false);
        }
        if (wd.feed().is_error()) {
            const uint32_t errors =
                watchdog_feed_errors.fetch_add(1, std::memory_order_relaxed) + 1U;
            if ((errors % 64U) == 1U) {
                ESP_LOGW(TAG, "watchdog feed failed in mqtt_pub (errors=%lu)",
                         static_cast<unsigned long>(errors));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void health_task(void* /*param*/) {
    // Low-frequency telemetry (30s cadence) must not subscribe to TWDT: default timeout is 5s.
    bool radio_stalled = false;
    bool pipeline_stalled = false;
    bool mqtt_stalled = false;

    while (true) {
        auto& wifi = wifi_manager::WifiManager::instance();
        auto& mqtt = mqtt_service::MqttService::instance();
        auto& health = health_monitor::HealthMonitor::instance();
        auto& ota = ota_manager::OtaManager::instance();
        const uint32_t now = now_ms();
        const auto cfg = config_store::ConfigStore::instance().config();

        wifi.poll_retry_timer();

        const bool radio_now_stalled =
            (now - radio_loop_last_ms.load(std::memory_order_relaxed)) > kCriticalTaskStallMs;
        const bool pipeline_now_stalled =
            (now - pipeline_loop_last_ms.load(std::memory_order_relaxed)) > kCriticalTaskStallMs;
        const bool mqtt_now_stalled =
            (now - mqtt_loop_last_ms.load(std::memory_order_relaxed)) > kCriticalTaskStallMs;

        if (radio_now_stalled && !radio_stalled) {
            radio_stall_count.fetch_add(1, std::memory_order_relaxed);
            health.report_warning("radio_rx task heartbeat stalled");
        }
        if (pipeline_now_stalled && !pipeline_stalled) {
            pipeline_stall_count.fetch_add(1, std::memory_order_relaxed);
            health.report_warning("pipeline task heartbeat stalled");
        }
        if (mqtt_now_stalled && !mqtt_stalled) {
            mqtt_stall_count.fetch_add(1, std::memory_order_relaxed);
            health.report_warning("mqtt task heartbeat stalled");
        }
        radio_stalled = radio_now_stalled;
        pipeline_stalled = pipeline_now_stalled;
        mqtt_stalled = mqtt_now_stalled;

        const bool any_critical_stalled = radio_stalled || pipeline_stalled || mqtt_stalled;
        if (!any_critical_stalled && wifi.state() == wifi_manager::WifiState::Connected &&
            mqtt.is_connected()) {
            health.report_healthy();
        } else if (wifi.state() == wifi_manager::WifiState::Disconnected) {
            health.report_warning("WiFi disconnected");
        } else if (!any_critical_stalled && !mqtt.is_connected()) {
            health.report_warning("MQTT disconnected");
        }

        if (!boot_valid_marked_) {
            const bool wifi_ready = wifi.state() == wifi_manager::WifiState::Connected;
            const bool mqtt_ready = !cfg.mqtt.enabled || mqtt.is_connected();
            const bool runtime_stable = !any_critical_stalled && now >= kBootValidationStableRuntimeMs;
            if (runtime_stable) {
                boot_valid_marked_ = true;
                if (!wifi_ready || !mqtt_ready) {
                    ESP_LOGW(
                        TAG,
                        "Boot-valid after stable runtime without full connectivity (wifi=%d mqtt=%d mqtt_enabled=%d)",
                        static_cast<int>(wifi_ready), static_cast<int>(mqtt.is_connected()),
                        static_cast<int>(cfg.mqtt.enabled));
                } else {
                    ESP_LOGI(TAG, "Boot-valid after stable runtime health gate");
                }
                auto boot_valid = ota.mark_boot_valid();
                if (boot_valid.is_error()) {
                    health.report_warning("mark_boot_valid failed after runtime health gate");
                }
            }
        }

        if (mqtt.is_connected()) {
            auto metrics_res = metrics_service::MetricsService::instance().snapshot();
            if (metrics_res.is_ok()) {
                const auto& m = metrics_res.value();
                const auto& tc = telegram_router::TelegramRouter::instance().counters();
                const auto ms = mqtt.status();
                const bool rx_active = radio_state_machine::RadioStateMachine::instance().state() ==
                                       radio_state_machine::RsmState::Receiving;

                auto command_result = mqtt_service::make_telemetry_command(
                    cfg.mqtt.prefix, cfg.device.hostname, static_cast<uint32_t>(m.uptime_s),
                    m.free_heap_bytes, m.min_free_heap_bytes, wifi.status().rssi_dbm,
                    mqtt.is_connected() ? "connected" : "disconnected",
                    rx_active ? "rx_active" : "idle", m.sessions.completed, tc.frames_published,
                    tc.frames_duplicate, m.sessions.link_rejected, ms.publish_count,
                    ms.publish_failures, "");
                if (command_result.is_ok()) {
                    enqueue_mqtt(command_result.value());
                } else {
                    mqtt.report_outbox_enqueue_failure(true);
                }
            }
        }
        ESP_LOGI(TAG, "Heap: free=%lu min=%lu",
                 static_cast<unsigned long>(esp_get_free_heap_size()),
                 static_cast<unsigned long>(esp_get_minimum_free_heap_size()));
        ESP_LOGI(
            TAG, "Stack HWM: radio=%lu pipeline=%lu mqtt=%lu health=%lu",
            static_cast<unsigned long>(radio_task_handle ? uxTaskGetStackHighWaterMark(radio_task_handle) : 0),
            static_cast<unsigned long>(pipeline_task_handle ? uxTaskGetStackHighWaterMark(pipeline_task_handle) : 0),
            static_cast<unsigned long>(mqtt_task_handle ? uxTaskGetStackHighWaterMark(mqtt_task_handle) : 0),
            static_cast<unsigned long>(health_task_handle ? uxTaskGetStackHighWaterMark(health_task_handle) : 0));
        sample_queue_levels();
        sample_task_stack_watermarks();

        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

} // namespace

namespace app_core {

common::Result<void> AppCore::create_runtime_tasks() {
    cleanup_runtime_resources();
    frame_enqueue_success.store(0, std::memory_order_relaxed);
    frame_enqueue_drop.store(0, std::memory_order_relaxed);
    frame_enqueue_errors.store(0, std::memory_order_relaxed);
    frame_queue_max_depth.store(0, std::memory_order_relaxed);
    frame_queue_send_failures.store(0, std::memory_order_relaxed);
    mqtt_outbox_enqueue_success.store(0, std::memory_order_relaxed);
    mqtt_outbox_enqueue_drop.store(0, std::memory_order_relaxed);
    mqtt_outbox_enqueue_errors.store(0, std::memory_order_relaxed);
    mqtt_outbox_peak_depth.store(0, std::memory_order_relaxed);
    const uint32_t now = now_ms();
    radio_loop_last_ms.store(now, std::memory_order_relaxed);
    pipeline_loop_last_ms.store(now, std::memory_order_relaxed);
    mqtt_loop_last_ms.store(now, std::memory_order_relaxed);
    pipeline_frames_processed.store(0, std::memory_order_relaxed);
    radio_stall_count.store(0, std::memory_order_relaxed);
    pipeline_stall_count.store(0, std::memory_order_relaxed);
    mqtt_stall_count.store(0, std::memory_order_relaxed);
    watchdog_register_errors.store(0, std::memory_order_relaxed);
    watchdog_feed_errors.store(0, std::memory_order_relaxed);
    boot_valid_marked_ = false;
    pipeline_config_dirty.store(false, std::memory_order_relaxed);
    metrics_service::MetricsService::reset_queue_metrics();
    metrics_service::MetricsService::reset_task_metrics();
    metrics_service::MetricsService::reset_session_metrics();

    frame_queue = xQueueCreate(kFrameQueueDepth, sizeof(RadioRxQueueItem));
    mqtt_outbox = xQueueCreate(kMqttOutboxDepth, sizeof(mqtt_service::MqttPublishCommand));
    if (!frame_queue || !mqtt_outbox) {
        ESP_LOGE(TAG, "Runtime queue allocation failed (frame_queue=%p mqtt_outbox=%p)",
                 frame_queue, mqtt_outbox);
        cleanup_runtime_resources();
        return common::Result<void>::error(common::ErrorCode::BufferFull);
    }

    auto& rsm = radio_state_machine::RadioStateMachine::instance();
    const auto pins = board_config::default_cc1101_pins();
    const auto bus = board_config::default_cc1101_spi_bus_config();
    ESP_LOGI(TAG, "Board CC1101 pins: MOSI=%d MISO=%d SCK=%d CS=%d GDO0=%d GDO2=%d", pins.mosi,
             pins.miso, pins.sck, pins.cs, pins.gdo0, pins.gdo2);
    ESP_LOGI(TAG, "CC1101 SPI bus: host=%d clock_hz=%lu max_transfer=%d", bus.host_id,
             static_cast<unsigned long>(bus.clock_hz), bus.max_transfer_size);

    auto rsm_init = rsm.initialize(pins, bus);
    if (rsm_init.is_error()) {
        ESP_LOGE(TAG, "Radio state machine initialize failed (%s/%d)",
                 common::error_code_to_string(rsm_init.error()),
                 static_cast<int>(rsm_init.error()));
        cleanup_runtime_resources();
        return rsm_init;
    }

    auto rx_start = rsm.start_receiving();
    if (rx_start.is_error()) {
        ESP_LOGE(TAG, "Radio RX start failed (%s/%d)",
                 common::error_code_to_string(rx_start.error()),
                 static_cast<int>(rx_start.error()));
        cleanup_runtime_resources();
        return rx_start;
    }

    if (xTaskCreatePinnedToCore(radio_rx_task, "radio_rx", 8192, nullptr, 10, &radio_task_handle,
                                1) !=
        pdPASS) {
        ESP_LOGE(TAG, "Failed to create task: radio_rx");
        cleanup_runtime_resources();
        return common::Result<void>::error(common::ErrorCode::Unknown);
    }
    if (xTaskCreatePinnedToCore(pipeline_task, "pipeline", 12288, nullptr, 7, &pipeline_task_handle,
                                0) !=
        pdPASS) {
        ESP_LOGE(TAG, "Failed to create task: pipeline");
        cleanup_runtime_resources();
        return common::Result<void>::error(common::ErrorCode::Unknown);
    }
    if (xTaskCreatePinnedToCore(mqtt_task, "mqtt_pub", 8192, nullptr, 5, &mqtt_task_handle, 0) !=
        pdPASS) {
        ESP_LOGE(TAG, "Failed to create task: mqtt_pub");
        cleanup_runtime_resources();
        return common::Result<void>::error(common::ErrorCode::Unknown);
    }
    if (xTaskCreatePinnedToCore(health_task, "health", 8192, nullptr, 3, &health_task_handle, 0) !=
        pdPASS) {
        ESP_LOGE(TAG, "Failed to create task: health");
        cleanup_runtime_resources();
        return common::Result<void>::error(common::ErrorCode::Unknown);
    }

    ESP_LOGI(
        TAG,
        "Runtime tasks created (radio_rx@Core1, pipeline@Core0, mqtt_pub@Core0, health@Core0)");
    sample_queue_levels();
    sample_task_stack_watermarks();
    return common::Result<void>::ok();
}

} // namespace app_core

#else

namespace app_core {

common::Result<void> AppCore::create_runtime_tasks() {
    return common::Result<void>::ok();
}

} // namespace app_core

#endif // HOST_TEST_BUILD
