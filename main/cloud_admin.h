#pragma once
#include <stdint.h>
#include "mqtt_client.h"

/* Called only by the MQTT command task. */
void cloud_admin_handle(const char *envelope, esp_mqtt_client_handle_t client,
                        const char *target, const char *reply_topic);
void cloud_admin_reset(void);
