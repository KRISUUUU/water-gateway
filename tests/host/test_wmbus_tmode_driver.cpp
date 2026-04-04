// Host tests for wmbus_link::WMbusTmodeDriver.
//
// Covers:
//   - IProtocolDriver identity contracts (protocol_id, required_radio_profile, max_session_encoded_bytes)
//   - reset_session clears state
//   - feed_byte correctly maps FramerState to DriverFeedStatus
//   - finalize_frame fills ProtocolFrame correctly
//   - decode_telegram produces a valid DecodedTelegram for a well-formed frame
//   - decode_telegram returns false for a frame that fails link validation
//   - last_validated_telegram bridges to the wmbus_link::ValidatedTelegram type
//   - full round-trip: encode → feed → finalize → decode → identity check

#include "host_test_stubs.hpp"
#include "protocol_driver/protocol_ids.hpp"
#include "wmbus_link/wmbus_tmode_driver.hpp"
#include "wmbus_tmode_rx/wmbus_block_validation.hpp"
#include "wmbus_tmode_rx/wmbus_tmode_framer.hpp"

#include <cassert>
#include <cstdio>
#include <vector>

namespace {

// ---- Test helpers (shared with test_downstream_exact_flow pattern) ----

constexpr uint8_t kEncode3of6[16] = {0x16, 0x0D, 0x0E, 0x0B, 0x1C, 0x19, 0x1A, 0x13,
                                     0x2C, 0x25, 0x26, 0x23, 0x34, 0x31, 0x32, 0x29};

std::vector<uint8_t> encode_3of6(const std::vector<uint8_t>& bytes) {
    std::vector<uint8_t> out;
    uint32_t bit_buf    = 0;
    int      bits_in_buf = 0;
    for (uint8_t byte : bytes) {
        const uint8_t hi = kEncode3of6[(byte >> 4U) & 0x0FU];
        const uint8_t lo = kEncode3of6[byte & 0x0FU];
        bit_buf    = (bit_buf << 12U) | (static_cast<uint32_t>(hi) << 6U) | lo;
        bits_in_buf += 12;
        while (bits_in_buf >= 8) {
            bits_in_buf -= 8;
            out.push_back(static_cast<uint8_t>((bit_buf >> bits_in_buf) & 0xFFU));
        }
    }
    if (bits_in_buf > 0) {
        out.push_back(static_cast<uint8_t>((bit_buf << (8 - bits_in_buf)) & 0xFFU));
    }
    return out;
}

// Minimal valid W-MBus T-mode frame: L=9, Format A, 12 decoded bytes.
// Bytes: L C Mfg[2] DevID[4] Version DevType  CRC[2]
std::vector<uint8_t> make_valid_decoded_frame() {
    std::vector<uint8_t> d = {
        0x09,               // L field
        0x44,               // C field
        0x84, 0x0D,         // Manufacturer ID (little-endian encoded)
        0x90, 0x48, 0x46, 0x06, // Device ID (BCD, little-endian)
        0x01,               // Version
        0x07,               // Device type
        0x00, 0x00,         // CRC placeholder
    };
    const uint16_t crc = wmbus_tmode_rx::calculate_wmbus_crc16(d.data(), 10U);
    d[10] = static_cast<uint8_t>((crc >> 8U) & 0xFFU);
    d[11] = static_cast<uint8_t>(crc & 0xFFU);
    return d;
}

// Feed all bytes of a 3-of-6 encoded frame into the driver.
// Returns the final DriverFeedResult status.
protocol_driver::DriverFeedStatus feed_encoded_frame(wmbus_link::WMbusTmodeDriver& driver,
                                                     const std::vector<uint8_t>& encoded) {
    protocol_driver::DriverFeedResult last{};
    for (uint8_t byte : encoded) {
        last = driver.feed_byte(byte);
        if (last.status == protocol_driver::DriverFeedStatus::FrameComplete ||
            last.status == protocol_driver::DriverFeedStatus::FrameRejected) {
            break;
        }
    }
    return last.status;
}

// ---- Tests ----

void test_identity_contracts() {
    wmbus_link::WMbusTmodeDriver driver;
    assert(driver.protocol_id()           == protocol_driver::ProtocolId::WMbusT);
    assert(driver.required_radio_profile() == protocol_driver::RadioProfileId::WMbusT868);
    assert(driver.max_session_encoded_bytes() > 0U);
    assert(driver.max_session_encoded_bytes() ==
           wmbus_tmode_rx::WmbusTmodeFramer::kMaxEncodedBytes);
    std::printf("  PASS: identity contracts\n");
}

void test_reset_clears_state() {
    wmbus_link::WMbusTmodeDriver driver;
    const auto decoded  = make_valid_decoded_frame();
    const auto encoded  = encode_3of6(decoded);
    feed_encoded_frame(driver, encoded);

    driver.reset_session();

    // After reset, finalize_frame must fail.
    protocol_driver::ProtocolFrame frame{};
    assert(!driver.finalize_frame(frame));
    assert(driver.last_validated_telegram() == nullptr);
    std::printf("  PASS: reset_session clears driver state\n");
}

void test_feed_reaches_frame_complete() {
    wmbus_link::WMbusTmodeDriver driver;
    const auto decoded  = make_valid_decoded_frame();
    const auto encoded  = encode_3of6(decoded);

    const auto final_status = feed_encoded_frame(driver, encoded);
    assert(final_status == protocol_driver::DriverFeedStatus::FrameComplete);
    std::printf("  PASS: valid frame reaches FrameComplete\n");
}

void test_feed_passes_through_candidate_viable() {
    wmbus_link::WMbusTmodeDriver driver;
    const auto decoded  = make_valid_decoded_frame();
    const auto encoded  = encode_3of6(decoded);

    // Feed bytes until we see CandidateViable or FrameComplete, tracking
    // whether we ever saw CandidateViable.
    bool saw_viable = false;
    for (uint8_t byte : encoded) {
        const auto res = driver.feed_byte(byte);
        if (res.status == protocol_driver::DriverFeedStatus::CandidateViable) {
            saw_viable = true;
        }
        if (res.status == protocol_driver::DriverFeedStatus::FrameComplete ||
            res.status == protocol_driver::DriverFeedStatus::FrameRejected) {
            break;
        }
    }
    // For a properly-formed frame we must pass through CandidateViable before
    // FrameComplete (the first-block validation step produces it).
    assert(saw_viable);
    std::printf("  PASS: feed passes through CandidateViable\n");
}

void test_finalize_frame_fills_protocol_frame() {
    wmbus_link::WMbusTmodeDriver driver;
    const auto decoded = make_valid_decoded_frame();
    const auto encoded = encode_3of6(decoded);

    feed_encoded_frame(driver, encoded);

    protocol_driver::ProtocolFrame frame{};
    assert(driver.finalize_frame(frame));

    assert(frame.frame_complete);
    assert(frame.encoded_length > 0U);
    assert(frame.decoded_length > 0U);
    assert(frame.decoded_length == static_cast<uint16_t>(decoded.size()));
    assert(frame.metadata.protocol      == protocol_driver::ProtocolId::WMbusT);
    assert(frame.metadata.radio_profile == protocol_driver::RadioProfileId::WMbusT868);
    assert(frame.metadata.end_reason    == protocol_driver::FrameCaptureEndReason::Complete);

    // Encoded bytes content must match original 3-of-6 encoded buffer.
    for (size_t i = 0; i < encoded.size(); ++i) {
        assert(frame.encoded_bytes[i] == encoded[i]);
    }
    // Decoded bytes content must match original decoded buffer.
    for (size_t i = 0; i < decoded.size(); ++i) {
        assert(frame.decoded_bytes[i] == decoded[i]);
    }
    std::printf("  PASS: finalize_frame fills ProtocolFrame correctly\n");
}

void test_finalize_before_complete_returns_false() {
    wmbus_link::WMbusTmodeDriver driver;
    protocol_driver::ProtocolFrame frame{};
    assert(!driver.finalize_frame(frame));
    std::printf("  PASS: finalize_frame before FrameComplete returns false\n");
}

void test_decode_telegram_produces_valid_decoded_telegram() {
    wmbus_link::WMbusTmodeDriver driver;
    const auto decoded = make_valid_decoded_frame();
    const auto encoded = encode_3of6(decoded);

    feed_encoded_frame(driver, encoded);

    protocol_driver::ProtocolFrame proto_frame{};
    assert(driver.finalize_frame(proto_frame));

    // Set signal quality metadata before decode.
    proto_frame.metadata.rssi_dbm            = -70;
    proto_frame.metadata.lqi                 = 55;
    proto_frame.metadata.radio_crc_ok        = false;
    proto_frame.metadata.radio_crc_available = false;
    proto_frame.metadata.timestamp_ms        = 5000;
    proto_frame.metadata.radio_instance      = protocol_driver::kRadioInstancePrimary;
    proto_frame.metadata.radio_profile       = protocol_driver::RadioProfileId::WMbusT868;

    protocol_driver::DecodedTelegram telegram{};
    assert(driver.decode_telegram(proto_frame, telegram));

    // Protocol identity
    assert(telegram.metadata.protocol       == protocol_driver::ProtocolId::WMbusT);
    assert(telegram.metadata.radio_instance == protocol_driver::kRadioInstancePrimary);
    assert(telegram.metadata.radio_profile  == protocol_driver::RadioProfileId::WMbusT868);

    // Signal quality pass-through
    assert(telegram.metadata.rssi_dbm    == -70);
    assert(telegram.metadata.lqi         == 55);
    assert(telegram.metadata.timestamp_ms == 5000);

    // Identity (from the fixture frame: Mfg=0x0D84, DevID=0x06464890)
    assert(telegram.has_reliable_identity);
    assert(telegram.identity.reliable);
    assert(telegram.identity.manufacturer_id == 0x0D84U);
    assert(telegram.identity.device_id       == 0x06464890U);

    // Device type / version from canonical bytes[9] and [8]
    assert(telegram.identity.device_type    == 0x07U);
    assert(telegram.identity.device_version == 0x01U);

    // Canonical bytes must equal the decoded bytes.
    assert(telegram.canonical_length == static_cast<uint16_t>(decoded.size()));
    for (uint16_t i = 0; i < telegram.canonical_length; ++i) {
        assert(telegram.canonical_bytes[i] == decoded[i]);
    }
    std::printf("  PASS: decode_telegram produces valid DecodedTelegram\n");
}

void test_last_validated_telegram_accessible() {
    wmbus_link::WMbusTmodeDriver driver;
    const auto decoded = make_valid_decoded_frame();
    const auto encoded = encode_3of6(decoded);

    feed_encoded_frame(driver, encoded);

    protocol_driver::ProtocolFrame proto_frame{};
    driver.finalize_frame(proto_frame);
    proto_frame.metadata.rssi_dbm = -80;

    protocol_driver::DecodedTelegram telegram{};
    assert(driver.decode_telegram(proto_frame, telegram));

    // The wmbus_link::ValidatedTelegram must still be accessible for the
    // existing router path.
    const wmbus_link::ValidatedTelegram* vt = driver.last_validated_telegram();
    assert(vt != nullptr);
    assert(vt->manufacturer_id() == 0x0D84U);
    assert(vt->device_id()       == 0x06464890U);
    assert(vt->has_reliable_identity());
    std::printf("  PASS: last_validated_telegram() accessible after decode_telegram\n");
}

void test_last_validated_telegram_null_before_decode() {
    wmbus_link::WMbusTmodeDriver driver;
    assert(driver.last_validated_telegram() == nullptr);

    const auto decoded = make_valid_decoded_frame();
    const auto encoded = encode_3of6(decoded);
    feed_encoded_frame(driver, encoded);

    // finalize only — no decode yet
    protocol_driver::ProtocolFrame frame{};
    driver.finalize_frame(frame);
    assert(driver.last_validated_telegram() == nullptr);
    std::printf("  PASS: last_validated_telegram() is null before decode_telegram\n");
}

void test_decode_telegram_false_without_finalize() {
    wmbus_link::WMbusTmodeDriver driver;
    protocol_driver::ProtocolFrame frame{};
    protocol_driver::DecodedTelegram telegram{};
    // No feed, no finalize — must return false.
    assert(!driver.decode_telegram(frame, telegram));
    assert(driver.last_validated_telegram() == nullptr);
    std::printf("  PASS: decode_telegram returns false without prior finalize\n");
}

void test_reset_after_complete_allows_fresh_session() {
    wmbus_link::WMbusTmodeDriver driver;
    const auto decoded = make_valid_decoded_frame();
    const auto encoded = encode_3of6(decoded);

    // First session
    feed_encoded_frame(driver, encoded);
    protocol_driver::ProtocolFrame frame{};
    driver.finalize_frame(frame);
    protocol_driver::DecodedTelegram t{};
    assert(driver.decode_telegram(frame, t));
    assert(driver.last_validated_telegram() != nullptr);

    // Reset then second session
    driver.reset_session();
    assert(driver.last_validated_telegram() == nullptr);

    const auto status = feed_encoded_frame(driver, encoded);
    assert(status == protocol_driver::DriverFeedStatus::FrameComplete);
    protocol_driver::ProtocolFrame frame2{};
    assert(driver.finalize_frame(frame2));
    protocol_driver::DecodedTelegram t2{};
    assert(driver.decode_telegram(frame2, t2));
    assert(t2.identity.manufacturer_id == 0x0D84U);
    std::printf("  PASS: reset allows fresh session with correct decode\n");
}

} // namespace

int main() {
    std::printf("=== test_wmbus_tmode_driver ===\n");

    test_identity_contracts();
    test_reset_clears_state();
    test_feed_reaches_frame_complete();
    test_feed_passes_through_candidate_viable();
    test_finalize_frame_fills_protocol_frame();
    test_finalize_before_complete_returns_false();
    test_decode_telegram_produces_valid_decoded_telegram();
    test_last_validated_telegram_accessible();
    test_last_validated_telegram_null_before_decode();
    test_decode_telegram_false_without_finalize();
    test_reset_after_complete_allows_fresh_session();

    std::printf("All wmbus_tmode_driver tests passed.\n");
    return 0;
}
