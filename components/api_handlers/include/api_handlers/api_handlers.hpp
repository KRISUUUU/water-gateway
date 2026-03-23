#pragma once

namespace api_handlers {

// Registers REST handlers on the ESP-IDF HTTP daemon. On device, `server` is
// `httpd_handle_t`; host tests pass an opaque pointer and omit implementation.
void register_all_handlers(void* server);

} // namespace api_handlers
