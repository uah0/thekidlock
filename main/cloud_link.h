#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t cloud_link_init(void);
esp_err_t cloud_link_start(void);
void cloud_link_record(const char *event, const char *source, const char *actor, const char *result);
unsigned cloud_link_pending(void);
unsigned cloud_link_dropped(void);
bool cloud_link_time_ready(void);
bool cloud_link_mqtt_ready(void);
const char *cloud_link_error(void);
