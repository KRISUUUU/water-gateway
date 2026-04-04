#pragma once

#include "protocol_driver/protocol_ids.hpp"

#include <cstddef>
#include <cstdint>

// RadioProtocolScheduler: data model describing which protocols are assigned
// to which radio instances and in what order.
//
// --- Scope (current) ---
// This is scaffolding for the multi-protocol scheduler. It is a pure data
// model and is NOT yet connected to the runtime owner task. Adding a slot here
// does not change the active receive path.
//
// --- One-radio multi-protocol operation ---
// A single CC1101 can only receive one protocol at a time. Multi-protocol
// support is achieved by time-sliced profile management: the scheduler assigns
// ProtocolSlots to the radio, and the owner task (future work) will cycle
// through enabled slots, applying the required RadioProfile for each slot's
// receive window before handing the session to the matching protocol driver.
//
// --- Two-radio expansion path ---
// RadioInstanceId is an index into the physical radio array. RadioInstance 0
// is the current CC1101. When a second CC1101 is added, it becomes instance 1.
// Each instance runs its own owner task and has its own slot list. The
// scheduler tracks both. Protocol logic (framing, link validation) is shared
// regardless of which radio instance captured the frame.
//
// --- Slot ordering ---
// Slots are evaluated in insertion order. The first enabled slot for a given
// radio instance is considered the active slot for that instance. In a future
// time-sliced implementation, the scheduler will advance through slots using
// a round-robin cursor per radio instance.

namespace protocol_driver {

struct ProtocolSlot {
    ProtocolId      protocol        = ProtocolId::Unknown;
    RadioProfileId  profile         = RadioProfileId::Unknown;
    RadioInstanceId radio_instance  = kRadioInstancePrimary;

    // When false the slot is registered but not scheduled. Useful for
    // temporarily disabling a protocol without removing its configuration.
    bool enabled = false;

    // Desired receive-window duration in milliseconds. 0 means "use the
    // driver's default / indefinite". Unused until the time-sliced scheduler
    // is implemented; stored now so configuration is forward-compatible.
    uint32_t slot_duration_ms = 0;
};

// Maximum registered protocol slots across all radio instances.
static constexpr size_t kMaxProtocolSlots = 8;

class RadioProtocolScheduler {
  public:
    RadioProtocolScheduler();

    // Register a slot. Returns false if the slot table is full.
    bool add_slot(const ProtocolSlot& slot);

    // Remove the first slot matching (protocol, radio_instance). Returns
    // false if no matching slot was found.
    bool remove_slot(ProtocolId protocol, RadioInstanceId radio_instance);

    // Total number of registered slots (enabled + disabled).
    size_t slot_count() const;

    // Access a slot by index [0, slot_count()). Returns nullptr if out of
    // range.
    const ProtocolSlot* slot_at(size_t index) const;

    // Return the first enabled slot for radio_instance, in insertion order.
    // Returns nullptr if no enabled slot is registered for that instance.
    // This represents the "currently active" profile for a given radio.
    const ProtocolSlot* active_slot_for_radio(RadioInstanceId radio_instance) const;

    // Remove all slots.
    void clear();

  private:
    ProtocolSlot slots_[kMaxProtocolSlots];
    size_t       slot_count_ = 0;
};

} // namespace protocol_driver
