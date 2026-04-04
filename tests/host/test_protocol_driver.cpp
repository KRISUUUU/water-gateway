// Host tests for components/protocol_driver pure abstractions.
//
// Covers:
//   - ProtocolId and RadioProfileId enum values and string labels
//   - RadioInstanceId constants
//   - RadioProtocolScheduler: add, remove, query, limit, ordering, disabled
//     slot handling, clear

#include "protocol_driver/protocol_ids.hpp"
#include "protocol_driver/radio_profile.hpp"
#include "protocol_driver/radio_protocol_scheduler.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace protocol_driver;

namespace {

// ----- ProtocolId -----

void test_protocol_id_known_values() {
    assert(static_cast<uint8_t>(ProtocolId::Unknown)    == 0);
    assert(static_cast<uint8_t>(ProtocolId::WMbusT)     == 1);
    assert(static_cast<uint8_t>(ProtocolId::WMbusPrios) == 2);
    std::printf("  PASS: ProtocolId known values\n");
}

void test_protocol_id_to_string() {
    assert(std::strcmp(protocol_id_to_string(ProtocolId::WMbusT),     "WMbusT")     == 0);
    assert(std::strcmp(protocol_id_to_string(ProtocolId::WMbusPrios), "WMbusPrios") == 0);
    assert(std::strcmp(protocol_id_to_string(ProtocolId::Unknown),    "Unknown")    == 0);
    std::printf("  PASS: protocol_id_to_string\n");
}

// ----- RadioProfileId -----

void test_radio_profile_id_known_values() {
    assert(static_cast<uint8_t>(RadioProfileId::Unknown)      == 0);
    assert(static_cast<uint8_t>(RadioProfileId::WMbusT868)    == 1);
    assert(static_cast<uint8_t>(RadioProfileId::WMbusPriosR3) == 2);
    assert(static_cast<uint8_t>(RadioProfileId::WMbusPriosR4) == 3);
    std::printf("  PASS: RadioProfileId known values\n");
}

void test_radio_profile_id_to_string() {
    assert(std::strcmp(radio_profile_id_to_string(RadioProfileId::WMbusT868),    "WMbusT868")    == 0);
    assert(std::strcmp(radio_profile_id_to_string(RadioProfileId::WMbusPriosR3), "WMbusPriosR3") == 0);
    assert(std::strcmp(radio_profile_id_to_string(RadioProfileId::WMbusPriosR4), "WMbusPriosR4") == 0);
    assert(std::strcmp(radio_profile_id_to_string(RadioProfileId::Unknown),      "Unknown")      == 0);
    std::printf("  PASS: radio_profile_id_to_string\n");
}

// ----- RadioInstanceId constants -----

void test_radio_instance_constants() {
    assert(kRadioInstancePrimary   == 0);
    assert(kRadioInstanceSecondary == 1);
    assert(kRadioInstancePrimary   != kRadioInstanceSecondary);
    std::printf("  PASS: RadioInstanceId constants\n");
}

// ----- RadioProfile validity -----

void test_radio_profile_invalid_when_default() {
    RadioProfile p{};
    assert(!p.is_valid());
    std::printf("  PASS: default RadioProfile is not valid\n");
}

void test_radio_profile_valid_with_register_table() {
    static const RadioProfileRegisterEntry kRegs[] = {
        {0x00, 0x29},
        {0x01, 0x2E},
    };
    RadioProfile p{};
    p.id               = RadioProfileId::WMbusT868;
    p.primary_protocol = ProtocolId::WMbusT;
    p.register_config  = kRegs;
    p.register_count   = 2;
    p.description      = "test profile";
    assert(p.is_valid());
    std::printf("  PASS: RadioProfile is valid with register table\n");
}

// ----- RadioProtocolScheduler -----

void test_scheduler_starts_empty() {
    RadioProtocolScheduler sched;
    assert(sched.slot_count() == 0);
    assert(sched.active_slot_for_radio(kRadioInstancePrimary)   == nullptr);
    assert(sched.active_slot_for_radio(kRadioInstanceSecondary) == nullptr);
    assert(sched.slot_at(0) == nullptr);
    std::printf("  PASS: scheduler starts empty\n");
}

void test_scheduler_add_single_slot() {
    RadioProtocolScheduler sched;
    ProtocolSlot slot{};
    slot.protocol       = ProtocolId::WMbusT;
    slot.profile        = RadioProfileId::WMbusT868;
    slot.radio_instance = kRadioInstancePrimary;
    slot.enabled        = true;
    slot.slot_duration_ms = 0;

    assert(sched.add_slot(slot));
    assert(sched.slot_count() == 1);

    const ProtocolSlot* active = sched.active_slot_for_radio(kRadioInstancePrimary);
    assert(active != nullptr);
    assert(active->protocol       == ProtocolId::WMbusT);
    assert(active->profile        == RadioProfileId::WMbusT868);
    assert(active->radio_instance == kRadioInstancePrimary);
    assert(active->enabled        == true);
    std::printf("  PASS: add single enabled slot\n");
}

void test_scheduler_disabled_slot_not_active() {
    RadioProtocolScheduler sched;
    ProtocolSlot slot{};
    slot.protocol       = ProtocolId::WMbusT;
    slot.profile        = RadioProfileId::WMbusT868;
    slot.radio_instance = kRadioInstancePrimary;
    slot.enabled        = false; // disabled

    sched.add_slot(slot);
    assert(sched.slot_count() == 1);
    // Disabled slot must not be returned as the active slot.
    assert(sched.active_slot_for_radio(kRadioInstancePrimary) == nullptr);
    std::printf("  PASS: disabled slot is not active\n");
}

void test_scheduler_remove_slot() {
    RadioProtocolScheduler sched;
    ProtocolSlot slot{};
    slot.protocol       = ProtocolId::WMbusT;
    slot.profile        = RadioProfileId::WMbusT868;
    slot.radio_instance = kRadioInstancePrimary;
    slot.enabled        = true;

    sched.add_slot(slot);
    assert(sched.slot_count() == 1);

    assert(sched.remove_slot(ProtocolId::WMbusT, kRadioInstancePrimary));
    assert(sched.slot_count() == 0);
    assert(sched.active_slot_for_radio(kRadioInstancePrimary) == nullptr);
    std::printf("  PASS: remove slot\n");
}

void test_scheduler_remove_nonexistent_returns_false() {
    RadioProtocolScheduler sched;
    assert(!sched.remove_slot(ProtocolId::WMbusT, kRadioInstancePrimary));
    std::printf("  PASS: remove non-existent slot returns false\n");
}

void test_scheduler_slot_at_index() {
    RadioProtocolScheduler sched;

    ProtocolSlot s1{};
    s1.protocol       = ProtocolId::WMbusT;
    s1.profile        = RadioProfileId::WMbusT868;
    s1.radio_instance = kRadioInstancePrimary;
    s1.enabled        = true;

    ProtocolSlot s2{};
    s2.protocol       = ProtocolId::WMbusPrios;
    s2.profile        = RadioProfileId::WMbusPriosR3;
    s2.radio_instance = kRadioInstanceSecondary;
    s2.enabled        = true;

    sched.add_slot(s1);
    sched.add_slot(s2);

    assert(sched.slot_count() == 2);
    assert(sched.slot_at(0) != nullptr);
    assert(sched.slot_at(0)->protocol == ProtocolId::WMbusT);
    assert(sched.slot_at(1) != nullptr);
    assert(sched.slot_at(1)->protocol == ProtocolId::WMbusPrios);
    assert(sched.slot_at(2) == nullptr); // out of bounds
    std::printf("  PASS: slot_at index access and out-of-bounds returns nullptr\n");
}

void test_scheduler_insertion_order_preserved_after_remove() {
    RadioProtocolScheduler sched;

    ProtocolSlot s1{};
    s1.protocol       = ProtocolId::WMbusT;
    s1.radio_instance = kRadioInstancePrimary;
    s1.enabled        = true;

    ProtocolSlot s2{};
    s2.protocol       = ProtocolId::WMbusPrios;
    s2.radio_instance = kRadioInstancePrimary;
    s2.enabled        = true;

    ProtocolSlot s3{};
    s3.protocol       = ProtocolId::WMbusT;
    s3.radio_instance = kRadioInstanceSecondary;
    s3.enabled        = true;

    sched.add_slot(s1);
    sched.add_slot(s2);
    sched.add_slot(s3);
    assert(sched.slot_count() == 3);

    // Remove the middle slot
    sched.remove_slot(ProtocolId::WMbusPrios, kRadioInstancePrimary);
    assert(sched.slot_count() == 2);

    // s1 and s3 should remain in original order
    assert(sched.slot_at(0)->protocol       == ProtocolId::WMbusT);
    assert(sched.slot_at(0)->radio_instance == kRadioInstancePrimary);
    assert(sched.slot_at(1)->protocol       == ProtocolId::WMbusT);
    assert(sched.slot_at(1)->radio_instance == kRadioInstanceSecondary);
    std::printf("  PASS: insertion order preserved after middle removal\n");
}

void test_scheduler_slot_limit_enforced() {
    RadioProtocolScheduler sched;
    ProtocolSlot slot{};
    slot.protocol = ProtocolId::WMbusT;
    slot.enabled  = true;

    for (size_t i = 0; i < kMaxProtocolSlots; ++i) {
        slot.radio_instance = static_cast<RadioInstanceId>(i);
        assert(sched.add_slot(slot));
    }
    assert(sched.slot_count() == kMaxProtocolSlots);

    // One more must fail.
    slot.radio_instance = 99;
    assert(!sched.add_slot(slot));
    assert(sched.slot_count() == kMaxProtocolSlots);
    std::printf("  PASS: slot limit enforced at kMaxProtocolSlots (%zu)\n",
                kMaxProtocolSlots);
}

void test_scheduler_clear() {
    RadioProtocolScheduler sched;
    ProtocolSlot slot{};
    slot.protocol       = ProtocolId::WMbusT;
    slot.radio_instance = kRadioInstancePrimary;
    slot.enabled        = true;
    sched.add_slot(slot);
    assert(sched.slot_count() == 1);

    sched.clear();
    assert(sched.slot_count() == 0);
    assert(sched.active_slot_for_radio(kRadioInstancePrimary) == nullptr);
    std::printf("  PASS: clear removes all slots\n");
}

void test_scheduler_two_radio_instances_independent() {
    RadioProtocolScheduler sched;

    ProtocolSlot p{};
    p.protocol       = ProtocolId::WMbusT;
    p.profile        = RadioProfileId::WMbusT868;
    p.radio_instance = kRadioInstancePrimary;
    p.enabled        = true;

    ProtocolSlot s{};
    s.protocol       = ProtocolId::WMbusPrios;
    s.profile        = RadioProfileId::WMbusPriosR3;
    s.radio_instance = kRadioInstanceSecondary;
    s.enabled        = true;

    sched.add_slot(p);
    sched.add_slot(s);

    const ProtocolSlot* ap = sched.active_slot_for_radio(kRadioInstancePrimary);
    const ProtocolSlot* as = sched.active_slot_for_radio(kRadioInstanceSecondary);

    assert(ap != nullptr && ap->protocol == ProtocolId::WMbusT);
    assert(as != nullptr && as->protocol == ProtocolId::WMbusPrios);
    std::printf("  PASS: two radio instances have independent active slots\n");
}

} // namespace

int main() {
    std::printf("=== test_protocol_driver ===\n");

    test_protocol_id_known_values();
    test_protocol_id_to_string();
    test_radio_profile_id_known_values();
    test_radio_profile_id_to_string();
    test_radio_instance_constants();
    test_radio_profile_invalid_when_default();
    test_radio_profile_valid_with_register_table();
    test_scheduler_starts_empty();
    test_scheduler_add_single_slot();
    test_scheduler_disabled_slot_not_active();
    test_scheduler_remove_slot();
    test_scheduler_remove_nonexistent_returns_false();
    test_scheduler_slot_at_index();
    test_scheduler_insertion_order_preserved_after_remove();
    test_scheduler_slot_limit_enforced();
    test_scheduler_clear();
    test_scheduler_two_radio_instances_independent();

    std::printf("All protocol_driver tests passed.\n");
    return 0;
}
