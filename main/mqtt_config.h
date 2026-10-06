#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define MQTT_HOST_MAX 192
#define MQTT_PREFIX_MAX 32
#define MQTT_PORT 1883
typedef struct {
    uint32_t version;
    bool enabled;
    uint16_t reserved_port; /* Keep the existing NVS blob layout. */
    char host[MQTT_HOST_MAX + 1];
    char client_id[32], prefix[MQTT_PREFIX_MAX + 1];
} mqtt_config_t;
esp_err_t mqtt_config_get(mqtt_config_t *out);
esp_err_t mqtt_config_save(const mqtt_config_t *config);
esp_err_t mqtt_config_validate(const mqtt_config_t *config);
