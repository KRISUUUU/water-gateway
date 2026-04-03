#include "wmbus_tmode_rx/rx_session_engine.hpp"

#include "radio_cc1101/cc1101_registers.hpp"

#include <algorithm>
#include <cstring>

namespace wmbus_tmode_rx {

namespace {

CandidateProgress best_progress(const FeedResult& feed_result) {
    const auto& normal = feed_result.normal;
    const auto& reversed = feed_result.reversed;
    if (normal.decoded_bytes_produced > reversed.decoded_bytes_produced) {
        return normal;
    }
    if (reversed.decoded_bytes_produced > normal.decoded_bytes_produced) {
        return reversed;
    }
    if (normal.active && !reversed.active) {
        return normal;
    }
    if (reversed.active && !normal.active) {
        return reversed;
    }
    return normal;
}

uint16_t saturating_elapsed_ms(uint32_t now_ms, uint32_t started_at_ms) {
    const uint32_t elapsed = now_ms - started_at_ms;
    return static_cast<uint16_t>(elapsed > 0xFFFFU ? 0xFFFFU : elapsed);
}

} // namespace

RxSessionEngine::RxSessionEngine(const SessionEngineConfig& config) : config_(config) {
    reset();
}

void RxSessionEngine::reset() {
    framer_.reset();
    session_active_ = false;
    session_started_at_ms_ = 0;
    last_progress_ms_ = 0;
    first_data_byte_ = 0;
    first_data_byte_valid_ = false;
    encoded_bytes_seen_ = 0;
    packet_mode_ = HybridPacketMode::Infinite;
    captured_prefix_.fill(0U);
    captured_prefix_length_ = 0;
    last_feed_result_ = {};
}

SessionProgressSnapshot RxSessionEngine::snapshot() const {
    const auto progress = best_progress(last_feed_result_);
    return {
        session_active_,
        session_started_at_ms_,
        last_progress_ms_,
        encoded_bytes_seen_,
        progress.decoded_bytes_produced,
        progress.exact_encoded_bytes_required,
        progress.expected_decoded_bytes,
        packet_mode_,
        progress.orientation,
        progress.l_field_known,
    };
}

uint32_t RxSessionEngine::no_progress_timeout_ms(const SessionEngineConfig& config,
                                                 const CandidateProgress& progress) {
    if (!progress.l_field_known || progress.exact_encoded_bytes_required == 0U) {
        return config.idle_poll_timeout_ms;
    }

    const uint32_t remaining_bytes =
        progress.exact_encoded_bytes_required > progress.encoded_bytes_seen
            ? static_cast<uint32_t>(progress.exact_encoded_bytes_required -
                                    progress.encoded_bytes_seen)
            : 0U;
    const uint32_t scaled_timeout =
        config.post_l_field_watchdog_floor_ms + std::min<uint32_t>(remaining_bytes / 2U, 12U);
    return std::max(config.min_watchdog_timeout_ms, scaled_timeout);
}

common::Result<SessionStepResult>
RxSessionEngine::process(SessionRadio& radio, const radio_cc1101::RadioOwnerEventSet& events,
                         uint32_t now_ms) {
    return process_impl(radio, events, now_ms);
}

common::Result<SessionStepResult>
RxSessionEngine::process_impl(SessionRadio& radio, const radio_cc1101::RadioOwnerEventSet& events,
                              uint32_t now_ms) {
    if (!events.should_attempt_rx_work() && !session_active_) {
        return common::Result<SessionStepResult>::ok(make_idle_result());
    }

    auto status_result = radio.read_status();
    if (status_result.is_error()) {
        return start_recovery(radio, SessionAbortReason::RadioSpiFailure,
                              status_result.error(), now_ms);
    }

    const auto status = status_result.value();
    if (status.fifo_overflow) {
        auto restart = radio.abort_and_restart_rx();
        if (restart.is_error()) {
            return start_recovery(radio, SessionAbortReason::RadioSpiFailure, restart.error(),
                                  now_ms);
        }
        auto result = make_diagnostic_result(SessionAbortReason::RadioOverflow,
                                             rf_diagnostics::RejectReason::RadioOverflow, now_ms);
        result.radio_error = common::ErrorCode::RadioFifoOverflow;
        clear_session();
        return common::Result<SessionStepResult>::ok(result);
    }

    if (session_active_ && status.fifo_bytes == 0U && watchdog_expired(now_ms)) {
        auto restart = radio.abort_and_restart_rx();
        if (restart.is_error()) {
            return start_recovery(radio, SessionAbortReason::RadioSpiFailure, restart.error(),
                                  now_ms);
        }
        auto result = make_diagnostic_result(SessionAbortReason::NoProgressTimeout,
                                             rf_diagnostics::RejectReason::SessionAborted, now_ms);
        result.radio_error = common::ErrorCode::RadioQualityDrop;
        clear_session();
        return common::Result<SessionStepResult>::ok(result);
    }

    if (status.fifo_bytes == 0U) {
        return common::Result<SessionStepResult>::ok(
            session_active_ ? make_progress_result(SessionStepState::SessionInProgress)
                            : make_idle_result());
    }

    uint8_t buffer[radio_cc1101::registers::FIFO_SIZE]{};
    SessionStepResult progress_result =
        session_active_ ? make_progress_result(SessionStepState::SessionInProgress)
                        : make_idle_result();

    SessionRadioStatus current_status = status;
    while (current_status.fifo_bytes > 0U) {
        const auto progress_before_read = best_progress(last_feed_result_);
        const uint16_t bounded_window =
            exact_read_window_bytes(progress_before_read, current_status.fifo_bytes);
        const uint16_t requested_window =
            bounded_window == 0U ? current_status.fifo_bytes : bounded_window;
        const uint16_t to_read =
            std::min<uint16_t>(requested_window, static_cast<uint16_t>(sizeof(buffer)));
        auto read_result = radio.read_fifo(buffer, to_read);
        if (read_result.is_error()) {
            return start_recovery(radio, SessionAbortReason::RadioSpiFailure, read_result.error(),
                                  now_ms);
        }
        const uint16_t read_count = read_result.value();
        if (read_count == 0U) {
            break;
        }

        if (!session_active_) {
            begin_session(now_ms);
        }

        for (uint16_t i = 0; i < read_count; ++i) {
            record_captured_byte(buffer[i]);
            last_feed_result_ = framer_.feed_byte(buffer[i]);
            encoded_bytes_seen_++;
            last_progress_ms_ = now_ms;

            if (last_feed_result_.state == FramerState::CandidateRejected) {
                auto restart = radio.abort_and_restart_rx();
                if (restart.is_error()) {
                    return start_recovery(radio, SessionAbortReason::RadioSpiFailure,
                                          restart.error(), now_ms);
                }
                auto result = make_diagnostic_result(
                    SessionAbortReason::CandidateRejected,
                    map_reject_reason(last_feed_result_.reject_reason), now_ms);
                result.radio_error = common::ErrorCode::RadioQualityDrop;
                clear_session();
                return common::Result<SessionStepResult>::ok(result);
            }

            auto switch_result = maybe_switch_to_fixed_length(radio, best_progress(last_feed_result_));
            if (switch_result.is_error()) {
                return start_recovery(radio, SessionAbortReason::RadioSpiFailure,
                                      switch_result.error(), now_ms);
            }

            if (last_feed_result_.state == FramerState::ExactFrameComplete &&
                last_feed_result_.has_complete_frame) {
                auto quality_result = radio.read_signal_quality();
                if (quality_result.is_error()) {
                    return start_recovery(radio, SessionAbortReason::RadioSpiFailure,
                                          quality_result.error(), now_ms);
                }
                auto result =
                    make_complete_frame_result(last_feed_result_, quality_result.value(), now_ms);
                if (packet_mode_ == HybridPacketMode::FixedLengthTail) {
                    auto restore_result = radio.restore_infinite_packet_mode();
                    if (restore_result.is_error()) {
                        result.radio_error = restore_result.error();
                    }
                }
                
                auto restart = radio.abort_and_restart_rx();
                if (restart.is_error() && result.radio_error == common::ErrorCode::OK) {
                    result.radio_error = restart.error();
                }

                clear_session();
                return common::Result<SessionStepResult>::ok(result);
            }
        }

        auto next_status_result = radio.read_status();
        if (next_status_result.is_error()) {
            return start_recovery(radio, SessionAbortReason::RadioSpiFailure,
                                  next_status_result.error(), now_ms);
        }
        current_status = next_status_result.value();
        if (current_status.fifo_overflow) {
            auto restart = radio.abort_and_restart_rx();
            if (restart.is_error()) {
                return start_recovery(radio, SessionAbortReason::RadioSpiFailure, restart.error(),
                                      now_ms);
            }
            auto result = make_diagnostic_result(SessionAbortReason::RadioOverflow,
                                                 rf_diagnostics::RejectReason::RadioOverflow,
                                                 now_ms);
            result.radio_error = common::ErrorCode::RadioFifoOverflow;
            clear_session();
            return common::Result<SessionStepResult>::ok(result);
        }
    }

    return common::Result<SessionStepResult>::ok(
        session_active_ ? make_progress_result(SessionStepState::SessionInProgress)
                        : progress_result);
}

common::Result<void>
RxSessionEngine::maybe_switch_to_fixed_length(SessionRadio& radio,
                                              const CandidateProgress& progress) {
    const auto decision = evaluate_packet_length_transition(progress, packet_mode_);
    if (!decision.should_switch) {
        return common::Result<void>::ok();
    }

    // CC1101-specific assumption: once the software framer knows the exact remaining encoded
    // byte budget, switching from infinite packet mode to fixed-length mode with that remaining
    // tail count will bound the remainder of the capture. Software framing and exact-length
    // checks remain authoritative, and this behavior still requires real hardware validation.
    auto switch_result = radio.switch_to_fixed_length(decision.fixed_length_register_value);
    if (switch_result.is_error()) {
        return common::Result<void>::error(switch_result.error());
    }
    packet_mode_ = decision.desired_mode;
    return common::Result<void>::ok();
}

common::Result<SessionStepResult>
RxSessionEngine::start_recovery(SessionRadio& /*radio*/, SessionAbortReason reason,
                                common::ErrorCode radio_error, uint32_t now_ms) {
    auto result =
        make_diagnostic_result(reason, rf_diagnostics::RejectReason::RadioSpiError, now_ms);
    result.state = SessionStepState::RecoveryRequested;
    result.radio_error = radio_error;
    clear_session();
    return common::Result<SessionStepResult>::ok(result);
}

SessionStepResult RxSessionEngine::make_idle_result() const {
    auto result = make_progress_result(SessionStepState::Idle);
    result.progress.active = false;
    return result;
}

SessionStepResult RxSessionEngine::make_progress_result(SessionStepState state) const {
    SessionStepResult result{};
    result.state = state;
    result.progress = snapshot();
    return result;
}

SessionStepResult RxSessionEngine::make_diagnostic_result(SessionAbortReason abort_reason,
                                                          rf_diagnostics::RejectReason reject_reason,
                                                          uint32_t now_ms) const {
    SessionStepResult result{};
    result.state = SessionStepState::DiagnosticReady;
    result.abort_reason = abort_reason;
    result.progress = snapshot();
    result.has_diagnostic = true;
    result.diagnostic.reject_reason = reject_reason;
    result.diagnostic.orientation = diagnostic_orientation(last_feed_result_);
    result.diagnostic.expected_encoded_length =
        best_progress(last_feed_result_).exact_encoded_bytes_required;
    result.diagnostic.actual_encoded_length = encoded_bytes_seen_;
    result.diagnostic.expected_decoded_length =
        best_progress(last_feed_result_).expected_decoded_bytes;
    result.diagnostic.actual_decoded_length =
        best_progress(last_feed_result_).decoded_bytes_produced;
    result.diagnostic.capture_elapsed_ms =
        session_active_ ? saturating_elapsed_ms(now_ms, session_started_at_ms_) : 0U;
    result.diagnostic.first_data_byte = first_data_byte_valid_ ? first_data_byte_ : 0U;
    result.diagnostic.quality_issue = true;
    result.diagnostic.captured_prefix_length = captured_prefix_length_;
    std::memcpy(result.diagnostic.captured_prefix.data(), captured_prefix_.data(),
                captured_prefix_length_);
    return result;
}

SessionStepResult RxSessionEngine::make_complete_frame_result(const FeedResult& feed_result,
                                                              const SessionSignalQuality& quality,
                                                              uint32_t now_ms) const {
    SessionStepResult result{};
    result.state = SessionStepState::ExactFrameComplete;
    result.progress = snapshot();
    result.has_frame = true;
    result.frame.candidate = feed_result.frame;
    result.frame.rssi_dbm = quality.rssi_dbm;
    result.frame.lqi = quality.lqi;
    result.frame.crc_ok = quality.crc_ok;
    result.frame.radio_crc_available = quality.radio_crc_available;
    result.frame.capture_elapsed_ms = saturating_elapsed_ms(now_ms, session_started_at_ms_);
    result.frame.first_data_byte = first_data_byte_valid_ ? first_data_byte_ : 0U;
    return result;
}

void RxSessionEngine::begin_session(uint32_t now_ms) {
    framer_.reset();
    last_feed_result_ = {};
    session_active_ = true;
    session_started_at_ms_ = now_ms;
    last_progress_ms_ = now_ms;
    first_data_byte_ = 0U;
    first_data_byte_valid_ = false;
    encoded_bytes_seen_ = 0U;
    packet_mode_ = HybridPacketMode::Infinite;
    captured_prefix_.fill(0U);
    captured_prefix_length_ = 0U;
}

void RxSessionEngine::clear_session() {
    framer_.reset();
    session_active_ = false;
    session_started_at_ms_ = 0U;
    last_progress_ms_ = 0U;
    first_data_byte_ = 0U;
    first_data_byte_valid_ = false;
    encoded_bytes_seen_ = 0U;
    packet_mode_ = HybridPacketMode::Infinite;
    captured_prefix_.fill(0U);
    captured_prefix_length_ = 0U;
    last_feed_result_ = {};
}

void RxSessionEngine::record_captured_byte(uint8_t byte) {
    if (!first_data_byte_valid_) {
        first_data_byte_ = byte;
        first_data_byte_valid_ = true;
    }
    if (captured_prefix_length_ < captured_prefix_.size()) {
        captured_prefix_[captured_prefix_length_++] = byte;
    }
}

bool RxSessionEngine::watchdog_expired(uint32_t now_ms) const {
    if (!session_active_) {
        return false;
    }
    const auto progress = best_progress(last_feed_result_);
    const uint32_t timeout_ms = no_progress_timeout_ms(config_, progress);
    return (now_ms - last_progress_ms_) >= timeout_ms;
}

rf_diagnostics::Orientation
RxSessionEngine::diagnostic_orientation(const FeedResult& feed_result) const {
    const auto progress = best_progress(feed_result);
    switch (progress.orientation) {
    case FrameOrientation::Normal:
        return rf_diagnostics::Orientation::Normal;
    case FrameOrientation::BitReversed:
        return rf_diagnostics::Orientation::BitReversed;
    case FrameOrientation::Unknown:
    default:
        return rf_diagnostics::Orientation::Unknown;
    }
}

rf_diagnostics::RejectReason RxSessionEngine::map_reject_reason(RejectReason reason) const {
    switch (reason) {
    case RejectReason::InvalidSymbol:
        return rf_diagnostics::RejectReason::Invalid3of6Symbol;
    case RejectReason::LengthOutOfRange:
        return rf_diagnostics::RejectReason::InvalidLength;
    case RejectReason::FirstBlockValidationFailed:
        return rf_diagnostics::RejectReason::FirstBlockValidationFailed;
    case RejectReason::ProgressExceeded:
        return rf_diagnostics::RejectReason::ExactLengthMismatch;
    case RejectReason::AmbiguousOrientation:
    case RejectReason::None:
    default:
        return rf_diagnostics::RejectReason::SessionAborted;
    }
}

} // namespace wmbus_tmode_rx
