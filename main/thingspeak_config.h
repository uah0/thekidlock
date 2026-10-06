#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define THINGSPEAK_LOGIN_MAX 64
#define THINGSPEAK_PASSWORD_MAX 64
#define THINGSPEAK_URL_MAX 192
#define THINGSPEAK_KEY_MAX 64

typedef struct {
    uint32_t channel_id;
    char channel_url[THINGSPEAK_URL_MAX + 1];
    char read_api_key[THINGSPEAK_KEY_MAX + 1];
    char write_api_key[THINGSPEAK_KEY_MAX + 1];
    bool enabled;
} thingspeak_config_t;

esp_err_t thingspeak_config_get(thingspeak_config_t *out);
/* Empty secret fields preserve their previous values. */
esp_err_t thingspeak_config_save(const thingspeak_config_t *config);
