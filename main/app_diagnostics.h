#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* Test-only local Agent API entry point. Pattern 1..6 plays that many pulses;
 * pattern 7 plays the overlap pattern (dash and three dots). */
esp_err_t app_buzzer_request_test(uint32_t pattern);
void app_ulp_diagnostics_set_verbose(bool enabled);
bool app_ulp_diagnostics_get_verbose(void);
