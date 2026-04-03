#include "rf_diagnostics/rf_diagnostics.hpp"

#include <algorithm>
#include <memory>

#include "cJSON.h"

namespace rf_diagnostics {

namespace {

using JsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
using JsonStringPtr = std::unique_ptr<char, decltype(&cJSON_free)>;

JsonPtr make_object() {
    return JsonPtr(cJSON_CreateObject(), cJSON_Delete);
}

JsonPtr make_array() {
    return JsonPtr(cJSON_CreateArray(), cJSON_Delete);
}

size_t bounded_length(size_t requested, size_t capacity) {
    return std::min(requested, capacity);
}

RfDiagnosticRecord sanitize_record(const RfDiagnosticRecord& input) {
    RfDiagnosticRecord out = input;
    out.captured_prefix_length = static_cast<uint8_t>(
        bounded_length(out.captured_prefix_length, out.captured_prefix.size()));
    out.decoded_prefix_length = static_cast<uint8_t>(
        bounded_length(out.decoded_prefix_length, out.decoded_prefix.size()));
    return out;
}

std::string to_unformatted_json(cJSON* root) {
    if (!root) {
        return "{}";
    }
    JsonStringPtr printed(cJSON_PrintUnformatted(root), cJSON_free);
    if (!printed) {
        return "{}";
    }
    return std::string(printed.get());
}

void add_record_json(cJSON* array, const RfDiagnosticRecord& record) {
    cJSON* node = cJSON_CreateObject();
    if (!node) {
        return;
    }

    cJSON_AddNumberToObject(node, "sequence", static_cast<double>(record.sequence));
    cJSON_AddNumberToObject(node, "timestamp_epoch_ms",
                            static_cast<double>(record.timestamp_epoch_ms));
    cJSON_AddNumberToObject(node, "monotonic_ms", static_cast<double>(record.monotonic_ms));
    cJSON_AddStringToObject(node, "reject_reason",
                            RfDiagnosticsService::reject_reason_to_string(record.reject_reason)
                                .c_str());
    cJSON_AddStringToObject(node, "orientation",
                            RfDiagnosticsService::orientation_to_string(record.orientation)
                                .c_str());
    cJSON_AddNumberToObject(node, "expected_encoded_length",
                            static_cast<double>(record.expected_encoded_length));
    cJSON_AddNumberToObject(node, "actual_encoded_length",
                            static_cast<double>(record.actual_encoded_length));
    cJSON_AddNumberToObject(node, "expected_decoded_length",
                            static_cast<double>(record.expected_decoded_length));
    cJSON_AddNumberToObject(node, "actual_decoded_length",
                            static_cast<double>(record.actual_decoded_length));
    cJSON_AddNumberToObject(node, "capture_elapsed_ms",
                            static_cast<double>(record.capture_elapsed_ms));
    cJSON_AddNumberToObject(node, "first_data_byte",
                            static_cast<double>(record.first_data_byte));
    cJSON_AddBoolToObject(node, "quality_issue", record.quality_issue);
    cJSON_AddBoolToObject(node, "signal_quality_valid", record.signal_quality_valid);
    cJSON_AddNumberToObject(node, "rssi_dbm", static_cast<double>(record.rssi_dbm));
    cJSON_AddNumberToObject(node, "lqi", static_cast<double>(record.lqi));
    cJSON_AddBoolToObject(node, "radio_crc_available", record.radio_crc_available);
    cJSON_AddBoolToObject(node, "radio_crc_ok", record.radio_crc_ok);
    cJSON_AddStringToObject(
        node, "captured_prefix_hex",
        RfDiagnosticsService::prefix_to_hex(record.captured_prefix.data(),
                                            record.captured_prefix_length)
            .c_str());
    cJSON_AddNumberToObject(node, "captured_prefix_length",
                            static_cast<double>(record.captured_prefix_length));
    cJSON_AddStringToObject(
        node, "decoded_prefix_hex",
        RfDiagnosticsService::prefix_to_hex(record.decoded_prefix.data(),
                                            record.decoded_prefix_length)
            .c_str());
    cJSON_AddNumberToObject(node, "decoded_prefix_length",
                            static_cast<double>(record.decoded_prefix_length));
    cJSON_AddItemToArray(array, node);
}

} // namespace

void RfDiagnosticsRingBuffer::clear() {
    storage_.fill(RfDiagnosticRecord{});
    head_ = 0;
    count_ = 0;
    total_inserted_ = 0;
    total_evicted_ = 0;
}

void RfDiagnosticsRingBuffer::insert(const RfDiagnosticRecord& record) {
    const RfDiagnosticRecord bounded_record = sanitize_record(record);
    if (count_ == storage_.size()) {
        storage_[head_] = bounded_record;
        head_ = (head_ + 1U) % storage_.size();
        ++total_inserted_;
        ++total_evicted_;
        return;
    }

    storage_[(head_ + count_) % storage_.size()] = bounded_record;
    ++count_;
    ++total_inserted_;
}

RfDiagnosticsSnapshot RfDiagnosticsRingBuffer::snapshot() const {
    RfDiagnosticsSnapshot out{};
    out.count = count_;
    out.total_inserted = total_inserted_;
    out.total_evicted = total_evicted_;
    for (size_t i = 0; i < count_; ++i) {
        out.records[i] = storage_[(head_ + i) % storage_.size()];
    }
    return out;
}

RfDiagnosticsService& RfDiagnosticsService::instance() {
    static RfDiagnosticsService service;
    return service;
}

void RfDiagnosticsService::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    ring_.clear();
}

void RfDiagnosticsService::insert(const RfDiagnosticRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    ring_.insert(record);
}

RfDiagnosticsSnapshot RfDiagnosticsService::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ring_.snapshot();
}

std::string RfDiagnosticsService::to_json(const RfDiagnosticsSnapshot& snapshot) {
    JsonPtr root = make_object();
    if (!root) {
        return "{}";
    }

    cJSON_AddNumberToObject(root.get(), "capacity",
                            static_cast<double>(RfDiagnosticsSnapshot::kMaxRecords));
    cJSON_AddNumberToObject(root.get(), "count", static_cast<double>(snapshot.count));
    cJSON_AddNumberToObject(root.get(), "total_inserted",
                            static_cast<double>(snapshot.total_inserted));
    cJSON_AddNumberToObject(root.get(), "total_evicted",
                            static_cast<double>(snapshot.total_evicted));

    JsonPtr recent = make_array();
    if (!recent) {
        return "{}";
    }
    for (size_t i = 0; i < snapshot.count; ++i) {
        add_record_json(recent.get(), snapshot.records[i]);
    }
    cJSON_AddItemToObject(root.get(), "recent_sessions", recent.release());
    return to_unformatted_json(root.get());
}

std::string RfDiagnosticsService::reject_reason_to_string(RejectReason reason) {
    switch (reason) {
    case RejectReason::None:
        return "none";
    case RejectReason::OversizedBurst:
        return "oversized_burst";
    case RejectReason::BurstTimeout:
        return "burst_timeout";
    case RejectReason::Invalid3of6Symbol:
        return "invalid_3of6_symbol";
    case RejectReason::InvalidLength:
        return "invalid_length";
    case RejectReason::FirstBlockValidationFailed:
        return "first_block_validation_failed";
    case RejectReason::BlockValidationFailed:
        return "block_validation_failed";
    case RejectReason::ExactLengthMismatch:
        return "exact_length_mismatch";
    case RejectReason::RadioOverflow:
        return "radio_overflow";
    case RejectReason::RadioSpiError:
        return "radio_spi_error";
    case RejectReason::QualityDrop:
        return "quality_drop";
    case RejectReason::TransitionalAdapterRejected:
        return "transitional_adapter_rejected";
    case RejectReason::SessionAborted:
        return "session_aborted";
    case RejectReason::FrameTooShort:
        return "frame_too_short";
    case RejectReason::IdentityUnavailable:
        return "identity_unavailable";
    case RejectReason::Unknown:
        return "unknown";
    }
    return "unknown";
}

std::string RfDiagnosticsService::orientation_to_string(Orientation orientation) {
    switch (orientation) {
    case Orientation::Unknown:
        return "unknown";
    case Orientation::Normal:
        return "normal";
    case Orientation::BitReversed:
        return "bit_reversed";
    }
    return "unknown";
}

std::string RfDiagnosticsService::prefix_to_hex(const uint8_t* data, size_t length) {
    static constexpr char kHexChars[] = "0123456789ABCDEF";
    if (!data || length == 0) {
        return "";
    }
    std::string out;
    out.reserve(length * 2U);
    const size_t safe_length = bounded_length(length, RfDiagnosticRecord::kMaxCapturedPrefixBytes);
    for (size_t i = 0; i < safe_length; ++i) {
        const uint8_t value = data[i];
        out.push_back(kHexChars[(value >> 4U) & 0x0FU]);
        out.push_back(kHexChars[value & 0x0FU]);
    }
    return out;
}

} // namespace rf_diagnostics
