#include "radio_cc1101/radio_cc1101.hpp"

namespace radio_cc1101 {

RadioCc1101& RadioCc1101::instance() {
    static RadioCc1101 radio;
    return radio;
}

common::Result<void> RadioCc1101::initialize() {
    if (initialized_) {
        return common::Result<void>(common::ErrorCode::AlreadyInitialized);
    }

    initialized_ = true;
    status_.state = RadioState::Idle;
    return common::Result<void>();
}

common::Result<void> RadioCc1101::reset() {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    status_.counters.resets += 1;
    status_.state = RadioState::Idle;
    return common::Result<void>();
}

common::Result<void> RadioCc1101::enter_rx_mode() {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    status_.counters.rx_entries += 1;
    status_.state = RadioState::Receiving;
    return common::Result<void>();
}

common::Result<void> RadioCc1101::recover_from_rx_error() {
    if (!initialized_) {
        return common::Result<void>(common::ErrorCode::NotInitialized);
    }

    status_.counters.recoveries += 1;
    status_.state = RadioState::Idle;
    return common::Result<void>();
}

common::Result<RawRadioFrame> RadioCc1101::read_frame() {
    if (!initialized_) {
        return common::Result<RawRadioFrame>(common::ErrorCode::NotInitialized);
    }

    if (status_.state != RadioState::Receiving) {
        return common::Result<RawRadioFrame>(common::ErrorCode::InvalidState);
    }

    RawRadioFrame frame{};
    frame.bytes = {0xAA, 0xBB, 0xCC};
    frame.rssi = -75;
    frame.lqi = 100;
    frame.crc_ok = true;

    status_.counters.frames_seen += 1;
    status_.last_rssi = frame.rssi;
    status_.last_lqi = frame.lqi;

    return common::Result<RawRadioFrame>(frame);
}

RadioStatus RadioCc1101::status() const {
    return status_;
}

}  // namespace radio_cc1101
