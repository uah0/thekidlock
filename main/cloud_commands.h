#pragma once

#include <stdbool.h>
#include "esp_err.h"
esp_err_t cloud_commands_init(void);
esp_err_t cloud_commands_start(void);
bool cloud_commands_connected(void);
bool cloud_commands_busy(void);
const char *cloud_commands_status(void);
