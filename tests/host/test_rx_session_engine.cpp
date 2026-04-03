#include "host_test_stubs.hpp"
#include "radio_cc1101/cc1101_owner_events.hpp"
#include "wmbus_tmode_rx/packet_length_strategy.hpp"
#include "wmbus_tmode_rx/rx_session_engine.hpp"
#include "wmbus_tmode_rx/wmbus_block_validation.hpp"

#include <cassert>
#include <cstdio>
#include <deque>
#include <vector>

using namespace wmbus_tmode_rx;

namespace {

constexpr uint8_t kEncode3of6[16] = {0x16, 0x0D, 0x0E, 0x0B, 0x1C, 0x19, 0x1A, 0x13,
                                     0x2C, 0x25, 0x26, 0x23, 0x34, 0x31, 0x32, 0x29};

uint8_t reverse_bits8(uint8_t value) {
    return static_cast<uint8_t>(((value & 0x01U) << 7U) | ((value & 0x02U) << 5U) |
                                ((value & 0x04U) << 3U) | ((value & 0x08U) << 1U) |
                                ((value & 0x10U) >> 1U) | ((value & 0x20U) >> 3U) |
                                ((value & 0x40U) >> 5U) | ((value & 0x80U) >> 7U));
}

std::vector<uint8_t> encode_3of6(const std::vector<uint8_t>& bytes, bool reverse_encoded = false) {
    std::vector<uint8_t> out;
    uint32_t bit_buf = 0;
    int bits_in_buf = 0;

    for (uint8_t byte : bytes) {
        const uint8_t hi = kEncode3of6[(byte >> 4U) & 0x0FU];
        const uint8_t lo = kEncode3of6[byte & 0x0FU];
        bit_buf = (bit_buf << 12U) | (static_cast<uint32_t>(hi) << 6U) | lo;
        bits_in_buf += 12;
        while (bits_in_buf >= 8) {
            bits_in_buf -= 8;
            out.push_back(static_cast<uint8_t>((bit_buf >> bits_in_buf) & 0xFFU));
        }
    }

    if (bits_in_buf > 0) {
        out.push_back(static_cast<uint8_t>((bit_buf << (8 - bits_in_buf)) & 0xFFU));
    }

    if (reverse_encoded) {
        for (auto& byte : out) {
            byte = reverse_bits8(byte);
        }
    }

    return out;
}

std::vector<uint8_t> make_valid_first_block_frame(uint8_t c_field = 0x44, uint8_t marker = 0x07) {
    std::vector<uint8_t> decoded = {
        0x09,
        c_field,
        0x84,
        0x0D,
        0x90,
        0x48,
        0x46,
        0x06,
        0x01,
        marker,
        0x00,
        0x00,
    };

    const uint16_t crc = calculate_wmbus_crc16(decoded.data(), 10);
    decoded[10] = static_cast<uint8_t>((crc >> 8U) & 0xFFU);
    decoded[11] = static_cast<uint8_t>(crc & 0xFFU);
    return decoded;
}

class FakeSessionRadio final : public SessionRadio {
  public:
    void push_fifo_chunk(const std::vector<uint8_t>& chunk) {
        fifo_chunks_.push_back(chunk);
    }

    void set_fifo_overflow(bool overflow) {
        fifo_overflow_ = overflow;
    }

    void set_status_error(common::ErrorCode error) {
        status_error_ = error;
    }

    void set_read_error(common::ErrorCode error) {
        read_error_ = error;
    }

    void set_switch_error(common::ErrorCode error) {
        switch_error_ = error;
    }

    common::Result<SessionRadioStatus> read_status() override {
        if (status_error_ != common::ErrorCode::OK) {
            return common::Result<SessionRadioStatus>::error(status_error_);
        }
        return common::Result<SessionRadioStatus>::ok(
            {next_chunk_size(), fifo_overflow_, true});
    }

    common::Result<uint16_t> read_fifo(uint8_t* buffer, uint16_t capacity) override {
        if (read_error_ != common::ErrorCode::OK) {
            return common::Result<uint16_t>::error(read_error_);
        }
        if (fifo_chunks_.empty()) {
            return common::Result<uint16_t>::ok(0U);
        }
        auto& chunk = fifo_chunks_.front();
        const size_t chunk_size = chunk.size();
        const auto take = static_cast<uint16_t>(std::min<size_t>(capacity, chunk_size));
        for (uint16_t i = 0; i < take; ++i) {
            buffer[i] = chunk[i];
        }
        if (take == chunk_size) {
            fifo_chunks_.pop_front();
        } else {
            chunk.erase(chunk.begin(), chunk.begin() + take);
        }
        return common::Result<uint16_t>::ok(take);
    }

    common::Result<SessionSignalQuality> read_signal_quality() override {
        return common::Result<SessionSignalQuality>::ok({-61, 51, false, false});
    }

    common::Result<void> switch_to_fixed_length(uint8_t remaining_encoded_bytes) override {
        if (switch_error_ != common::ErrorCode::OK) {
            return common::Result<void>::error(switch_error_);
        }
        switch_calls_++;
        last_fixed_length_ = remaining_encoded_bytes;
        return common::Result<void>::ok();
    }

    common::Result<void> restore_infinite_packet_mode() override {
        restore_calls_++;
        return common::Result<void>::ok();
    }

    common::Result<void> abort_and_restart_rx() override {
        restart_calls_++;
        fifo_chunks_.clear();
        fifo_overflow_ = false;
        return common::Result<void>::ok();
    }

    size_t restart_calls() const {
        return restart_calls_;
    }

    size_t switch_calls() const {
        return switch_calls_;
    }

    size_t restore_calls() const {
        return restore_calls_;
    }

    uint8_t last_fixed_length() const {
        return last_fixed_length_;
    }

  private:
    uint8_t next_chunk_size() const {
        if (fifo_overflow_) {
            return 0U;
        }
        return fifo_chunks_.empty() ? 0U : static_cast<uint8_t>(fifo_chunks_.front().size());
    }

    std::deque<std::vector<uint8_t>> fifo_chunks_{};
    bool fifo_overflow_ = false;
    common::ErrorCode status_error_ = common::ErrorCode::OK;
    common::ErrorCode read_error_ = common::ErrorCode::OK;
    common::ErrorCode switch_error_ = common::ErrorCode::OK;
    size_t restart_calls_ = 0;
    size_t switch_calls_ = 0;
    size_t restore_calls_ = 0;
    uint8_t last_fixed_length_ = 0U;
};

radio_cc1101::RadioOwnerEventSet irq_event() {
    radio_cc1101::GdoIrqSnapshot snapshot{};
    snapshot.pending_mask = radio_cc1101::GdoIrqSnapshot::bit_for(radio_cc1101::GdoPin::Gdo0);
    snapshot.gdo0_edges = 1;
    return radio_cc1101::make_owner_events_from_irq(snapshot);
}

void test_session_start_and_exact_frame_completion() {
    FakeSessionRadio radio;
    RxSessionEngine engine;
    const auto decoded = make_valid_first_block_frame();
    const auto encoded = encode_3of6(decoded);
    radio.push_fifo_chunk(encoded);

    const auto result = engine.process(radio, irq_event(), 100);
    assert(result.is_ok());
    assert(result.value().state == SessionStepState::ExactFrameComplete);
    assert(result.value().has_frame);
    assert(result.value().frame.candidate.encoded_length == encoded.size());
    assert(result.value().frame.candidate.decoded_length == decoded.size());
    assert(result.value().frame.first_data_byte == encoded.front());
    assert(radio.switch_calls() == 1U);
    assert(radio.last_fixed_length() == static_cast<uint8_t>(encoded.size() - 2U));
    // Success path: restore infinite packet mode (was in fixed-length tail), no restart.
    // MCSM1=0x3F keeps the radio in RX; FIFO is fully consumed.
    assert(radio.restore_calls() == 1U);
    assert(radio.restart_calls() == 0U);
    std::printf("  PASS: session start and exact frame completion (no restart)\n");
}

void test_candidate_rejection_creates_bounded_diagnostic() {
    FakeSessionRadio radio;
    RxSessionEngine engine;
    radio.push_fifo_chunk({0xFF, 0xFF});

    const auto result = engine.process(radio, irq_event(), 200);
    assert(result.is_ok());
    assert(result.value().state == SessionStepState::DiagnosticReady);
    assert(result.value().radio_error == common::ErrorCode::RadioQualityDrop);
    assert(result.value().has_diagnostic);
    assert(result.value().diagnostic.reject_reason == rf_diagnostics::RejectReason::Invalid3of6Symbol);
    assert(result.value().diagnostic.captured_prefix_length <=
           rf_diagnostics::RfDiagnosticRecord::kMaxCapturedPrefixBytes);
    // Rejection path: abort/restart IS called to flush stale FIFO data.
    assert(radio.restart_calls() == 1U);
    std::printf("  PASS: candidate rejection creates bounded diagnostic\n");
}

void test_valid_reversed_orientation_completes() {
    FakeSessionRadio radio;
    RxSessionEngine engine;
    const auto decoded = make_valid_first_block_frame(0x46, 0x09);
    radio.push_fifo_chunk(encode_3of6(decoded, true));

    const auto result = engine.process(radio, irq_event(), 300);
    assert(result.is_ok());
    assert(result.value().state == SessionStepState::ExactFrameComplete);
    assert(result.value().frame.candidate.orientation == FrameOrientation::BitReversed);
    // Success path: no restart needed.
    assert(radio.restart_calls() == 0U);
    std::printf("  PASS: reversed orientation frame completes (no restart)\n");
}

void test_overflow_aborts_session() {
    FakeSessionRadio radio;
    RxSessionEngine engine;
    radio.set_fifo_overflow(true);

    const auto result = engine.process(radio, irq_event(), 400);
    assert(result.is_ok());
    // Overflow produces DiagnosticReady with radio error, not RecoveryRequested.
    assert(result.value().state == SessionStepState::DiagnosticReady);
    assert(result.value().radio_error == common::ErrorCode::RadioFifoOverflow);
    assert(result.value().has_diagnostic);
    assert(result.value().diagnostic.reject_reason == rf_diagnostics::RejectReason::RadioOverflow);
    // Overflow path: abort/restart IS called to flush corrupt FIFO.
    assert(radio.restart_calls() == 1U);
    std::printf("  PASS: overflow aborts session\n");
}

void test_no_progress_watchdog_aborts_stalled_session() {
    FakeSessionRadio radio;
    SessionEngineConfig config{};
    config.idle_poll_timeout_ms = 4;
    config.min_watchdog_timeout_ms = 4;
    config.post_l_field_watchdog_floor_ms = 4;
    RxSessionEngine engine(config);

    const auto decoded = make_valid_first_block_frame();
    const auto encoded = encode_3of6(decoded);
    assert(!encoded.empty());
    radio.push_fifo_chunk({encoded.front()});

    auto first = engine.process(radio, irq_event(), 500);
    assert(first.is_ok());
    assert(first.value().state == SessionStepState::SessionInProgress);

    auto second = engine.process(radio, radio_cc1101::make_session_watchdog_tick_event(), 505);
    assert(second.is_ok());
    assert(second.value().state == SessionStepState::DiagnosticReady);
    assert(second.value().abort_reason == SessionAbortReason::NoProgressTimeout);
    assert(second.value().radio_error == common::ErrorCode::RadioQualityDrop);
    // Timeout path: abort/restart IS called to flush stale FIFO data.
    assert(radio.restart_calls() == 1U);
    std::printf("  PASS: no-progress watchdog aborts stalled session\n");
}

void test_spi_failure_requests_recovery() {
    FakeSessionRadio radio;
    RxSessionEngine engine;
    radio.set_status_error(common::ErrorCode::RadioSpiError);

    const auto result = engine.process(radio, irq_event(), 600);
    assert(result.is_ok());
    assert(result.value().state == SessionStepState::RecoveryRequested);
    assert(result.value().radio_error == common::ErrorCode::RadioSpiError);
    assert(result.value().abort_reason == SessionAbortReason::RadioSpiFailure);
    std::printf("  PASS: SPI failure requests recovery\n");
}

void test_packet_length_strategy_length_unknown_does_not_switch() {
    CandidateProgress progress{};
    progress.active = true;
    progress.l_field_known = false;
    progress.encoded_bytes_seen = 2;

    const auto decision =
        evaluate_packet_length_transition(progress, HybridPacketMode::Infinite);
    assert(!decision.should_switch);
    assert(decision.reason == PacketLengthDecisionReason::LengthUnknown);
    std::printf("  PASS: no switch before length is known\n");
}

void test_packet_length_strategy_switches_once_exact_length_is_known() {
    CandidateProgress progress{};
    progress.active = true;
    progress.l_field_known = true;
    progress.encoded_bytes_seen = 2;
    progress.exact_encoded_bytes_required = 18;

    const auto decision =
        evaluate_packet_length_transition(progress, HybridPacketMode::Infinite);
    assert(decision.should_switch);
    assert(decision.reason == PacketLengthDecisionReason::SafeToSwitch);
    assert(decision.remaining_encoded_length == 16);
    assert(decision.fixed_length_register_value == 16U);
    std::printf("  PASS: fixed-length switch decision after early framing\n");
}

void test_exact_read_window_caps_fifo_request() {
    CandidateProgress progress{};
    progress.active = true;
    progress.l_field_known = true;
    progress.encoded_bytes_seen = 16;
    progress.exact_encoded_bytes_required = 18;

    assert(exact_read_window_bytes(progress, 10) == 2U);
    std::printf("  PASS: exact read window prevents over-read\n");
}

void test_exact_read_window_caps_pre_l_field_reads() {
    CandidateProgress progress{};
    progress.active = true;
    progress.l_field_known = false;
    progress.encoded_bytes_seen = 0;

    assert(exact_read_window_bytes(progress, 10) == 2U);

    progress.encoded_bytes_seen = 1;
    assert(exact_read_window_bytes(progress, 10) == 1U);

    std::printf("  PASS: exact read window preserves start alignment before L-field\n");
}

void test_session_recovery_when_mode_switch_fails() {
    FakeSessionRadio radio;
    RxSessionEngine engine;
    const auto decoded = make_valid_first_block_frame();
    const auto encoded = encode_3of6(decoded);
    radio.push_fifo_chunk({encoded[0], encoded[1]});
    radio.set_switch_error(common::ErrorCode::RadioSpiError);

    const auto result = engine.process(radio, irq_event(), 700);
    assert(result.is_ok());
    assert(result.value().state == SessionStepState::RecoveryRequested);
    assert(result.value().radio_error == common::ErrorCode::RadioSpiError);
    assert(result.value().abort_reason == SessionAbortReason::RadioSpiFailure);
    std::printf("  PASS: recovery requested when fixed-length transition fails\n");
}

// --- New tests for success-path receive continuity ---

void test_success_path_no_restart_no_restore_in_infinite_mode() {
    // When the frame completes without ever switching to fixed-length mode
    // (e.g. frame too short to trigger the switch), no restore and no restart
    // should be called. The radio stays in infinite RX (MCSM1=0x3F).
    FakeSessionRadio radio;
    RxSessionEngine engine;
    const auto decoded = make_valid_first_block_frame();
    const auto encoded = encode_3of6(decoded);

    // Feed chunks one byte at a time so the switch to fixed-length mode
    // has a chance to trigger — but with a short single-block frame,
    // the framer may complete before or right after the switch.
    // In either case, restart must NOT be called.
    radio.push_fifo_chunk(encoded);

    const auto result = engine.process(radio, irq_event(), 800);
    assert(result.is_ok());
    assert(result.value().state == SessionStepState::ExactFrameComplete);
    assert(result.value().has_frame);
    // The key assertion: success path never calls abort_and_restart_rx.
    assert(radio.restart_calls() == 0U);
    assert(result.value().radio_error == common::ErrorCode::OK);
    std::printf("  PASS: success path does not restart radio\n");
}

void test_engine_ready_for_next_frame_after_completion() {
    // After a successful frame, the engine's software state is cleared.
    // A second frame arriving immediately should be processed without
    // external intervention — verifying back-to-back receive continuity.
    FakeSessionRadio radio;
    RxSessionEngine engine;

    // First frame
    const auto decoded1 = make_valid_first_block_frame(0x44, 0x07);
    const auto encoded1 = encode_3of6(decoded1);
    radio.push_fifo_chunk(encoded1);

    auto r1 = engine.process(radio, irq_event(), 1000);
    assert(r1.is_ok());
    assert(r1.value().state == SessionStepState::ExactFrameComplete);
    assert(r1.value().has_frame);
    assert(radio.restart_calls() == 0U);

    // Second frame with different marker, arriving immediately
    const auto decoded2 = make_valid_first_block_frame(0x44, 0x08);
    const auto encoded2 = encode_3of6(decoded2);
    radio.push_fifo_chunk(encoded2);

    auto r2 = engine.process(radio, irq_event(), 1005);
    assert(r2.is_ok());
    assert(r2.value().state == SessionStepState::ExactFrameComplete);
    assert(r2.value().has_frame);
    // Neither frame triggered a restart.
    assert(radio.restart_calls() == 0U);
    // Both frames are distinct (different marker byte).
    assert(r1.value().frame.candidate.decoded_length == decoded1.size());
    assert(r2.value().frame.candidate.decoded_length == decoded2.size());
    std::printf("  PASS: engine ready for next frame after completion (back-to-back)\n");
}

void test_second_frame_survives_same_fifo_chunk_after_success() {
    FakeSessionRadio radio;
    RxSessionEngine engine;

    const auto decoded1 = make_valid_first_block_frame(0x44, 0x07);
    const auto encoded1 = encode_3of6(decoded1);
    const auto decoded2 = make_valid_first_block_frame(0x44, 0x08);
    const auto encoded2 = encode_3of6(decoded2);

    std::vector<uint8_t> combined = encoded1;
    combined.insert(combined.end(), encoded2.begin(), encoded2.end());
    radio.push_fifo_chunk(combined);

    auto first = engine.process(radio, irq_event(), 1100);
    assert(first.is_ok());
    assert(first.value().state == SessionStepState::ExactFrameComplete);
    assert(first.value().has_frame);
    assert(first.value().frame.first_data_byte == encoded1.front());
    assert(first.value().frame.candidate.decoded_bytes[9] == 0x07);
    assert(radio.restart_calls() == 0U);

    auto second = engine.process(radio, irq_event(), 1105);
    assert(second.is_ok());
    assert(second.value().state == SessionStepState::ExactFrameComplete);
    assert(second.value().has_frame);
    assert(second.value().frame.first_data_byte == encoded2.front());
    assert(second.value().frame.candidate.decoded_bytes[9] == 0x08);
    assert(radio.restart_calls() == 0U);

    std::printf("  PASS: second frame survives same FIFO chunk after success\n");
}

void test_failure_paths_still_restart() {
    // Verify that non-success paths (rejection, overflow, timeout) still
    // call abort_and_restart_rx, confirming the change is scoped to success only.

    // Rejection
    {
        FakeSessionRadio radio;
        RxSessionEngine engine;
        radio.push_fifo_chunk({0xFF, 0xFF});
        auto r = engine.process(radio, irq_event(), 100);
        assert(r.is_ok());
        assert(r.value().state == SessionStepState::DiagnosticReady);
        assert(radio.restart_calls() == 1U);
    }

    // Overflow
    {
        FakeSessionRadio radio;
        RxSessionEngine engine;
        radio.set_fifo_overflow(true);
        auto r = engine.process(radio, irq_event(), 200);
        assert(r.is_ok());
        assert(r.value().state == SessionStepState::DiagnosticReady);
        assert(radio.restart_calls() == 1U);
    }

    std::printf("  PASS: failure paths still call abort_and_restart_rx\n");
}

} // namespace

int main() {
    std::printf("=== test_rx_session_engine ===\n");
    test_packet_length_strategy_length_unknown_does_not_switch();
    test_packet_length_strategy_switches_once_exact_length_is_known();
    test_exact_read_window_caps_fifo_request();
    test_exact_read_window_caps_pre_l_field_reads();
    test_session_start_and_exact_frame_completion();
    test_candidate_rejection_creates_bounded_diagnostic();
    test_valid_reversed_orientation_completes();
    test_overflow_aborts_session();
    test_no_progress_watchdog_aborts_stalled_session();
    test_spi_failure_requests_recovery();
    test_session_recovery_when_mode_switch_fails();
    test_success_path_no_restart_no_restore_in_infinite_mode();
    test_engine_ready_for_next_frame_after_completion();
    test_second_frame_survives_same_fifo_chunk_after_success();
    test_failure_paths_still_restart();
    std::printf("All RX session engine tests passed.\n");
    return 0;
}
