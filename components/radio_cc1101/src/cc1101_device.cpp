#include "cc1101_device.hpp"

#include "cc1101_hal.hpp"

namespace radio_cc1101::device {

namespace {

bool safe_read_register(void* spi_handle, RadioCounters& counters, uint8_t addr, uint8_t& value) {
    if (!hal::spi_read_register(spi_handle, addr, value)) {
        counters.spi_errors++;
        return false;
    }
    return true;
}

bool safe_write_register(void* spi_handle, RadioCounters& counters, uint8_t addr, uint8_t value) {
    if (!hal::spi_write_register(spi_handle, addr, value)) {
        counters.spi_errors++;
        return false;
    }
    return true;
}

} // namespace

common::Result<void> apply_register_profile(void* spi_handle, RadioCounters& counters,
                                            const TmodeRegisterConfig* profile, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (!safe_write_register(spi_handle, counters, profile[i].addr, profile[i].value)) {
            return common::Result<void>::error(common::ErrorCode::RadioSpiError);
        }
    }
    return common::Result<void>::ok();
}

common::Result<uint8_t> read_marcstate(void* spi_handle, RadioCounters& counters) {
    uint8_t marcstate = 0;
    if (!safe_read_register(spi_handle, counters, registers::MARCSTATE | registers::READ_BURST,
                            marcstate)) {
        return common::Result<uint8_t>::error(common::ErrorCode::RadioSpiError);
    }
    return common::Result<uint8_t>::ok(static_cast<uint8_t>(marcstate & 0x1FU));
}

bool verify_chip_id(void* spi_handle, RadioCounters& counters, uint8_t& partnum, uint8_t& version) {
    return safe_read_register(spi_handle, counters, registers::PARTNUM | registers::READ_BURST,
                              partnum) &&
           safe_read_register(spi_handle, counters, registers::VERSION | registers::READ_BURST,
                              version);
}

int8_t convert_rssi(uint8_t raw_rssi) {
    int16_t rssi = raw_rssi >= 128U ? static_cast<int16_t>(raw_rssi) - 256
                                    : static_cast<int16_t>(raw_rssi);
    rssi = rssi / 2 - 74;
    return static_cast<int8_t>(rssi);
}

void record_drop(RadioDropInfo& last_drop, std::mutex& last_drop_mutex, RadioDropReason reason,
                 const uint8_t* data, uint16_t length, bool quality_issue) {
    std::lock_guard<std::mutex> lock(last_drop_mutex);
    last_drop.reason = reason;
    last_drop.captured_length = length;
    last_drop.elapsed_ms = 0;
    last_drop.first_data_byte = (data && length > 0U) ? data[0] : 0U;
    last_drop.quality_issue = quality_issue;
    last_drop.prefix_length = static_cast<uint8_t>(length < sizeof(last_drop.prefix)
                                                       ? length
                                                       : sizeof(last_drop.prefix));
    for (uint8_t i = 0; i < last_drop.prefix_length; ++i) {
        last_drop.prefix[i] = data[i];
    }
    for (uint8_t i = last_drop.prefix_length; i < sizeof(last_drop.prefix); ++i) {
        last_drop.prefix[i] = 0U;
    }
}

} // namespace radio_cc1101::device
