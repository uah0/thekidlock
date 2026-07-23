#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef void (*ap_mode_callback_t)(bool configuration_mode, bool erase_bonds);

esp_err_t ap_mode_init(ap_mode_callback_t callback);
bool ap_mode_is_configuration(void);
void ap_mode_set_configuration(bool enabled);
