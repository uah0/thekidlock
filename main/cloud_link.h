#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t cloud_link_init(void);
esp_err_t cloud_link_start(void);
bool cloud_link_time_ready(void);
