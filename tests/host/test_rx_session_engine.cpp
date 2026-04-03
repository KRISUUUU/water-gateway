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
        0x0B,
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
        auto chunk = fifo_chunks_.front();
        fifo_chunks_.pop_front();
        const auto take = static_cast<uint16_t>(std::min<size_t>(capacity, chunk.size()));
        for (uint16_t i = 0; i < take; ++i) {
            buffer[i] = chunk[i];
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
    assert(radio.restore_calls() == 1U);
    std::printf("  PASS: session start and exact frame completion\n");
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
    std::printf("  PASS: reversed orientation frame completes\n");
}

void test_overflow_requests_recovery() {
    FakeSessionRadio radio;
    RxSessionEngine engine;
    radio.set_fifo_overflow(true);

    const auto result = engine.process(radio, irq_event(), 400);
    assert(result.is_ok());
    assert(result.value().state == SessionStepState::RecoveryRequested);
    assert(result.value().radio_error == common::ErrorCode::RadioFifoOverflow);
    assert(result.value().has_diagnostic);
    assert(result.value().diagnostic.reject_reason == rf_diagnostics::RejectReason::RadioOverflow);
    std::printf("  PASS: overflow requests recovery\n");
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

    auto second = engine.process(radio, radio_cc1101::make_poll_tick_event(), 505);
    assert(second.is_ok());
    assert(second.value().state == SessionStepState::DiagnosticReady);
    assert(second.value().abort_reason == SessionAbortReason::NoProgressTimeout);
    assert(second.value().radio_error == common::ErrorCode::RadioQualityDrop);
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

} // namespace

int main() {
    std::printf("=== test_rx_session_engine ===\n");
    test_packet_length_strategy_length_unknown_does_not_switch();
    test_packet_length_strategy_switches_once_exact_length_is_known();
    test_exact_read_window_caps_fifo_request();
    test_session_start_and_exact_frame_completion();
    test_candidate_rejection_creates_bounded_diagnostic();
    test_valid_reversed_orientation_completes();
    test_overflow_requests_recovery();
    test_no_progress_watchdog_aborts_stalled_session();
    test_spi_failure_requests_recovery();
    test_session_recovery_when_mode_switch_fails();
    std::printf("All RX session engine tests passed.\n");
    return 0;
}
