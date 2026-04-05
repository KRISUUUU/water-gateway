#pragma once

#include "wmbus_prios_rx/prios_capture_service.hpp"

#include <string>

namespace wmbus_prios_rx {

// Appends one PRIOS capture export object to `out`.
//
// The output matches the fixture JSON used by the diagnostics export endpoint.
// It keeps serialization heap-bounded by emitting directly into a caller-owned
// string instead of building a cJSON tree for the full retained snapshot.
void append_export_json_object(std::string& out, const PriosCaptureRecord& record);

} // namespace wmbus_prios_rx
