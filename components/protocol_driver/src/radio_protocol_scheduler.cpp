#include "protocol_driver/radio_protocol_scheduler.hpp"

namespace protocol_driver {

RadioProtocolScheduler::RadioProtocolScheduler() : slots_{}, slot_count_(0) {
}

bool RadioProtocolScheduler::add_slot(const ProtocolSlot& slot) {
    if (slot_count_ >= kMaxProtocolSlots) {
        return false;
    }
    slots_[slot_count_] = slot;
    ++slot_count_;
    return true;
}

bool RadioProtocolScheduler::remove_slot(ProtocolId protocol,
                                         RadioInstanceId radio_instance) {
    for (size_t i = 0; i < slot_count_; ++i) {
        if (slots_[i].protocol == protocol &&
            slots_[i].radio_instance == radio_instance) {
            // Shift remaining slots down to preserve insertion order.
            for (size_t j = i; j + 1 < slot_count_; ++j) {
                slots_[j] = slots_[j + 1];
            }
            --slot_count_;
            return true;
        }
    }
    return false;
}

size_t RadioProtocolScheduler::slot_count() const {
    return slot_count_;
}

const ProtocolSlot* RadioProtocolScheduler::slot_at(size_t index) const {
    if (index >= slot_count_) {
        return nullptr;
    }
    return &slots_[index];
}

const ProtocolSlot* RadioProtocolScheduler::active_slot_for_radio(
    RadioInstanceId radio_instance) const {
    for (size_t i = 0; i < slot_count_; ++i) {
        if (slots_[i].radio_instance == radio_instance && slots_[i].enabled) {
            return &slots_[i];
        }
    }
    return nullptr;
}

void RadioProtocolScheduler::clear() {
    for (size_t i = 0; i < slot_count_; ++i) {
        slots_[i] = ProtocolSlot{};
    }
    slot_count_ = 0;
}

} // namespace protocol_driver
