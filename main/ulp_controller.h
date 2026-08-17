#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "shared_state.h"

/* The ULP owns the solenoid gate and ADC pins after initialization. */
esp_err_t ulp_controller_init(void);
void ulp_controller_request(ulp_command_t command);
bool ulp_controller_read_state(lock_state_t *state, uint32_t *faults,
                               uint32_t *events);
