#include "thingspeak_config.h"

#include <stdio.h>
#include <string.h>
#include "nvs.h"

#define THINGSPEAK_NVS_NS "thingspeak"

static void read_string(nvs_handle_t h, const char *key, char *out, size_t capacity)
{
    size_t length = capacity;
    if (nvs_get_str(h, key, out, &length) != ESP_OK) out[0] = '\0';
}

esp_err_t thingspeak_config_get(thingspeak_config_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    nvs_handle_t h;
    esp_err_t err = nvs_open(THINGSPEAK_NVS_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    read_string(h, "login", out->login, sizeof(out->login));
    read_string(h, "password", out->password, sizeof(out->password));
    read_string(h, "read_key", out->read_api_key, sizeof(out->read_api_key));
    read_string(h, "write_key", out->write_api_key, sizeof(out->write_api_key));
    (void)nvs_get_u32(h, "channel_id", &out->channel_id);
    nvs_close(h);
    if (out->channel_id) {
        snprintf(out->channel_url, sizeof(out->channel_url),
                 "https://thingspeak.mathworks.com/channels/%lu",
                 (unsigned long)out->channel_id);
    }
    return ESP_OK;
}

esp_err_t thingspeak_config_save(const thingspeak_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    char channel_url[THINGSPEAK_URL_MAX + 1] = "";
    if (config->channel_id) {
        snprintf(channel_url, sizeof(channel_url),
                 "https://thingspeak.mathworks.com/channels/%lu",
                 (unsigned long)config->channel_id);
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(THINGSPEAK_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, "login", config->login);
    if (err == ESP_OK) err = nvs_set_u32(h, "channel_id", config->channel_id);
    if (err == ESP_OK) err = nvs_set_str(h, "channel_url", channel_url);
    if (err == ESP_OK && config->password[0]) err = nvs_set_str(h, "password", config->password);
    if (err == ESP_OK && config->read_api_key[0]) err = nvs_set_str(h, "read_key", config->read_api_key);
    if (err == ESP_OK && config->write_api_key[0]) err = nvs_set_str(h, "write_key", config->write_api_key);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
