#include "thingspeak_config.h"

#include <stdio.h>
#include <string.h>
#include "nvs.h"

#define THINGSPEAK_NVS_NS "thingspeak"

static bool safe_api_key(const char *key)
{
    for (const char *p = key; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')))
            return false;
    return true;
}

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
    size_t blob_size = sizeof(*out);
    esp_err_t blob_err = nvs_get_blob(h, "cloud_config", out, &blob_size);
    if (blob_err == ESP_OK && blob_size == sizeof(*out)) { nvs_close(h); return ESP_OK; }
    if (blob_err != ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); return blob_err == ESP_OK ? ESP_ERR_INVALID_SIZE : blob_err; }
    read_string(h, "login", out->login, sizeof(out->login));
    read_string(h, "password", out->password, sizeof(out->password));
    read_string(h, "read_key", out->read_api_key, sizeof(out->read_api_key));
    read_string(h, "write_key", out->write_api_key, sizeof(out->write_api_key));
    (void)nvs_get_u32(h, "channel_id", &out->channel_id);
    uint8_t enabled = 0, commands = 0;
    (void)nvs_get_u8(h, "enabled", &enabled);
    (void)nvs_get_u8(h, "commands", &commands);
    out->enabled = enabled == 1;
    out->commands_enabled = commands == 1;
    (void)nvs_get_u32(h, "command_chan", &out->command_channel_id);
    (void)nvs_get_u32(h, "result_chan", &out->result_channel_id);
    read_string(h, "mqtt_client", out->mqtt_client_id, sizeof(out->mqtt_client_id));
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
    thingspeak_config_t merged = *config, previous;
    esp_err_t err = thingspeak_config_get(&previous);
    if (err != ESP_OK) return err;
    if (!merged.password[0]) strcpy(merged.password, previous.password);
    if (!merged.read_api_key[0]) strcpy(merged.read_api_key, previous.read_api_key);
    if (!merged.write_api_key[0]) strcpy(merged.write_api_key, previous.write_api_key);
    if (!safe_api_key(merged.read_api_key) || !safe_api_key(merged.write_api_key))
        return ESP_ERR_INVALID_ARG;
    if (merged.enabled && (!merged.channel_id || !merged.read_api_key[0] || !merged.write_api_key[0]))
        return ESP_ERR_INVALID_ARG;
    if (merged.commands_enabled && (!merged.enabled || !merged.command_channel_id ||
        !merged.result_channel_id || !merged.mqtt_client_id[0] || !merged.login[0] ||
        !merged.password[0] || merged.command_channel_id == merged.result_channel_id ||
        merged.channel_id == merged.command_channel_id || merged.channel_id == merged.result_channel_id))
        return ESP_ERR_INVALID_ARG;
    merged.channel_url[0] = '\0';
    if (merged.channel_id) snprintf(merged.channel_url, sizeof(merged.channel_url),
        "https://thingspeak.mathworks.com/channels/%lu", (unsigned long)merged.channel_id);
    nvs_handle_t h;
    err = nvs_open(THINGSPEAK_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    /* One NVS value avoids readers observing a partially changed configuration. */
    err = nvs_set_blob(h, "cloud_config", &merged, sizeof(merged));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
