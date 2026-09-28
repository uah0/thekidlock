#pragma once
#include <stddef.h>
#include "esp_err.h"
esp_err_t lock_ai_adapter_init(void);
/* Shared with the existing Web UI; no HTTP dependency. */
esp_err_t lock_ai_status_json(char *out, size_t capacity);
esp_err_t lock_ai_ulp_json(char *out, size_t capacity);
