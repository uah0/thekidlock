#pragma once

#include <stdbool.h>
#include "shared_state.h"

// Thread-safe mailbox write for the ULP command channel. GPIO8 is never
// accessed by the main CPU directly.
void lock_controller_request(ulp_command_t requested);

// Returns true once for a pending ULP event. The flags remain latched in
// shared RTC memory and can also be inspected directly by the application.
bool lock_controller_take_event(uint32_t *flags);
