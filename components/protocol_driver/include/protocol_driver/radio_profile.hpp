#pragma once

#include "protocol_driver/protocol_ids.hpp"

#include <cstddef>
#include <cstdint>

// RadioProfile: a descriptor that binds a RadioProfileId to the concrete
// CC1101 register configuration it requires.
//
// The register list format mirrors the existing TmodeRegisterConfig pattern
// in radio_cc1101/cc1101_profile_tmode.hpp but is expressed here as a
// protocol-layer type so that protocol drivers can declare their requirements
// without taking a direct dependency on radio_cc1101 internals.
//
// The actual register bytes are supplied by the radio layer (radio_cc1101)
// and referenced here via pointer; no copy is made.

namespace protocol_driver {

// A single (address, value) CC1101 register entry, mirroring the radio_cc1101
// convention to allow zero-copy binding.
struct RadioProfileRegisterEntry {
    uint8_t addr;
    uint8_t value;
};

struct RadioProfile {
    // Which profile this descriptor represents.
    RadioProfileId id = RadioProfileId::Unknown;

    // The protocol for which this profile is the primary configuration.
    // A profile may physically support other protocols but this names the
    // intended primary use.
    ProtocolId primary_protocol = ProtocolId::Unknown;

    // Pointer to a statically-allocated register table. Must not be null
    // when the profile is in use. Lifetime must exceed the scheduler.
    const RadioProfileRegisterEntry* register_config = nullptr;
    size_t register_count = 0;

    // Human-readable description for diagnostics and logs.
    const char* description = nullptr;

    bool is_valid() const {
        return id != RadioProfileId::Unknown
            && register_config != nullptr
            && register_count > 0;
    }
};

} // namespace protocol_driver
