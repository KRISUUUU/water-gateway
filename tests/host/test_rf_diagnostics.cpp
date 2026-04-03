#include "rf_diagnostics/rf_diagnostics.hpp"

#include <array>
#include <cassert>
#include <string>

namespace {

rf_diagnostics::RfDiagnosticRecord make_record(uint32_t sequence, uint8_t seed) {
    rf_diagnostics::RfDiagnosticRecord record{};
    record.sequence = sequence;
    record.timestamp_epoch_ms = 1000 + sequence;
    record.monotonic_ms = 2000 + sequence;
    record.reject_reason = rf_diagnostics::RejectReason::BurstTimeout;
    record.orientation = rf_diagnostics::Orientation::Normal;
    record.expected_encoded_length = 64;
    record.actual_encoded_length = static_cast<uint16_t>(20 + seed);
    record.expected_decoded_length = 32;
    record.actual_decoded_length = static_cast<uint16_t>(10 + seed);
    record.capture_elapsed_ms = 7;
    record.first_data_byte = seed;
    record.quality_issue = true;
    record.signal_quality_valid = true;
    record.rssi_dbm = -78;
    record.lqi = 55;
    record.radio_crc_available = false;
    record.radio_crc_ok = false;
    record.captured_prefix_length = 4;
    record.captured_prefix[0] = seed;
    record.captured_prefix[1] = static_cast<uint8_t>(seed + 1U);
    record.captured_prefix[2] = static_cast<uint8_t>(seed + 2U);
    record.captured_prefix[3] = static_cast<uint8_t>(seed + 3U);
    record.decoded_prefix_length = 3;
    record.decoded_prefix[0] = static_cast<uint8_t>(seed + 4U);
    record.decoded_prefix[1] = static_cast<uint8_t>(seed + 5U);
    record.decoded_prefix[2] = static_cast<uint8_t>(seed + 6U);
    return record;
}

} // namespace

int main() {
    rf_diagnostics::RfDiagnosticsRingBuffer ring;

    ring.insert(make_record(1, 0x10));
    ring.insert(make_record(2, 0x20));
    auto snapshot = ring.snapshot();
    assert(snapshot.count == 2);
    assert(snapshot.total_inserted == 2);
    assert(snapshot.total_evicted == 0);
    assert(snapshot.records[0].sequence == 1);
    assert(snapshot.records[1].sequence == 2);

    for (uint32_t i = 0; i < rf_diagnostics::RfDiagnosticsSnapshot::kMaxRecords + 3U; ++i) {
        ring.insert(make_record(100 + i, static_cast<uint8_t>(i)));
    }
    snapshot = ring.snapshot();
    assert(snapshot.count == rf_diagnostics::RfDiagnosticsSnapshot::kMaxRecords);
    assert(snapshot.total_inserted == rf_diagnostics::RfDiagnosticsSnapshot::kMaxRecords + 5U);
    assert(snapshot.total_evicted == 5U);
    assert(snapshot.records[0].sequence == 103);
    assert(snapshot.records[snapshot.count - 1].sequence == 118);

    rf_diagnostics::RfDiagnosticRecord bounded = make_record(999, 0xAB);
    bounded.captured_prefix_length = 99;
    bounded.decoded_prefix_length = 88;
    rf_diagnostics::RfDiagnosticsRingBuffer bounded_ring;
    bounded_ring.insert(bounded);
    const auto bounded_snapshot = bounded_ring.snapshot();
    assert(bounded_snapshot.records[0].captured_prefix_length ==
           rf_diagnostics::RfDiagnosticRecord::kMaxCapturedPrefixBytes);
    assert(bounded_snapshot.records[0].decoded_prefix_length ==
           rf_diagnostics::RfDiagnosticRecord::kMaxDecodedPrefixBytes);

    rf_diagnostics::RfDiagnosticsSnapshot json_snapshot{};
    json_snapshot.count = 1;
    json_snapshot.total_inserted = 7;
    json_snapshot.total_evicted = 2;
    json_snapshot.records[0] = make_record(77, 0x2A);
    const std::string json = rf_diagnostics::RfDiagnosticsService::to_json(json_snapshot);
    assert(json.find("\"capacity\":16") != std::string::npos);
    assert(json.find("\"count\":1") != std::string::npos);
    assert(json.find("\"total_inserted\":7") != std::string::npos);
    assert(json.find("\"total_evicted\":2") != std::string::npos);
    assert(json.find("\"reject_reason\":\"burst_timeout\"") != std::string::npos);
    assert(json.find("\"orientation\":\"normal\"") != std::string::npos);
    assert(json.find("\"captured_prefix_hex\":\"2A2B2C2D\"") != std::string::npos);
    assert(json.find("\"decoded_prefix_hex\":\"2E2F30\"") != std::string::npos);

    return 0;
}
