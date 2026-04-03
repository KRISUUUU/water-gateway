#include "host_test_stubs.hpp"
#include "mqtt_service/mqtt_publish.hpp"
#include "telegram_router/telegram_router.hpp"
#include "wmbus_link/wmbus_link.hpp"
#include "wmbus_tmode_rx/wmbus_block_validation.hpp"
#include "wmbus_tmode_rx/wmbus_tmode_framer.hpp"

#include <cassert>
#include <cstdio>
#include <vector>

namespace {

constexpr uint8_t kEncode3of6[16] = {0x16, 0x0D, 0x0E, 0x0B, 0x1C, 0x19, 0x1A, 0x13,
                                     0x2C, 0x25, 0x26, 0x23, 0x34, 0x31, 0x32, 0x29};

std::vector<uint8_t> encode_3of6(const std::vector<uint8_t>& bytes) {
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
    return out;
}

std::vector<uint8_t> make_valid_frame() {
    std::vector<uint8_t> decoded = {
        0x09, 0x44, 0x84, 0x0D, 0x90, 0x48, 0x46, 0x06, 0x01, 0x07, 0x00, 0x00,
    };
    const uint16_t crc = wmbus_tmode_rx::calculate_wmbus_crc16(decoded.data(), 10);
    decoded[10] = static_cast<uint8_t>((crc >> 8U) & 0xFFU);
    decoded[11] = static_cast<uint8_t>(crc & 0xFFU);
    return decoded;
}

wmbus_link::EncodedRxFrame exact_frame_from_decoded(const std::vector<uint8_t>& decoded) {
    wmbus_tmode_rx::WmbusTmodeFramer framer;
    wmbus_tmode_rx::FeedResult result{};
    const auto encoded = encode_3of6(decoded);
    for (uint8_t byte : encoded) {
        result = framer.feed_byte(byte);
    }
    assert(result.has_complete_frame);
    wmbus_link::EncodedRxFrame frame{};
    frame.encoded_length = result.frame.encoded_length;
    frame.decoded_length = result.frame.decoded_length;
    frame.exact_encoded_bytes_required = result.frame.exact_encoded_bytes_required;
    frame.l_field = result.frame.l_field;
    frame.orientation = result.frame.orientation;
    frame.first_block_validation = result.frame.first_block_validation;
    for (uint16_t i = 0; i < frame.encoded_length; ++i) {
        frame.encoded_bytes[i] = result.frame.encoded_bytes[i];
    }
    for (uint16_t i = 0; i < frame.decoded_length; ++i) {
        frame.decoded_bytes[i] = result.frame.decoded_bytes[i];
    }
    frame.metadata.exact_frame_contract_valid = true;
    frame.metadata.timestamp_ms = 1000;
    frame.metadata.rx_count = 3;
    frame.metadata.rssi_dbm = -61;
    frame.metadata.lqi = 51;
    frame.metadata.captured_frame_length = frame.encoded_length;
    frame.metadata.first_data_byte = frame.encoded_bytes[0];
    return frame;
}

} // namespace

int main() {
    const auto exact_frame = exact_frame_from_decoded(make_valid_frame());
    const auto link_result = wmbus_link::WmbusLink::validate_and_build(exact_frame);
    assert(link_result.accepted);

    auto route = telegram_router::TelegramRouter::instance().route(link_result.telegram);
    assert(route.publish_raw);

    auto command_result = mqtt_service::make_raw_frame_command(
        "wg", "dev1", link_result.telegram.link.canonical_bytes.data(),
        link_result.telegram.link.metadata.canonical_length,
        link_result.telegram.link.metadata.rssi_dbm, link_result.telegram.link.metadata.lqi,
        link_result.telegram.link.metadata.crc_ok,
        link_result.telegram.link.metadata.radio_crc_available,
        link_result.telegram.manufacturer_id(), link_result.telegram.device_id(),
        link_result.telegram.identity_key().c_str(), "monotonic_ms:1000",
        link_result.telegram.link.metadata.rx_count, true,
        link_result.telegram.exact_frame.metadata.exact_frame_contract_valid);
    assert(command_result.is_ok());

    const auto serialized = mqtt_service::serialize_publish_command(command_result.value());
    assert(serialized.is_ok());
    assert(std::string(serialized.value().topic) == "wg/dev1/rf/raw");
    assert(std::string(serialized.value().payload).find("\"meter_key\":\"mfg:0D84-id:06464890\"") !=
           std::string::npos);
    assert(std::string(serialized.value().payload).find("\"raw_frame_contract_valid\":true") !=
           std::string::npos);
    return 0;
}
