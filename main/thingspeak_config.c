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

/* Read-only migration layout of the former combined journal/MQTT configuration. */
typedef struct {
    char login[65],password[65];
    uint32_t channel_id;
    char channel_url[193],read_api_key[65],write_api_key[65];
    bool enabled,commands_enabled;
    uint32_t command_channel_id,result_channel_id;
    char mqtt_client_id[65];
} legacy_config_t;
esp_err_t thingspeak_config_get(thingspeak_config_t *out)
{
    if(!out) return ESP_ERR_INVALID_ARG;
    memset(out,0,sizeof(*out));
    nvs_handle_t h; esp_err_t err=nvs_open(THINGSPEAK_NVS_NS,NVS_READONLY,&h);
    if(err==ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if(err!=ESP_OK) return err;
    size_t size=sizeof(*out);
    err=nvs_get_blob(h,"journal_config",out,&size);
    if(err==ESP_OK) {
        nvs_close(h); return size==sizeof(*out) ? ESP_OK : ESP_ERR_INVALID_SIZE;
    }
    if(err!=ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); return err; }
    legacy_config_t old={0};size=sizeof(old);
    err=nvs_get_blob(h,"cloud_config",&old,&size);
    if(err==ESP_OK && size==sizeof(old)) {
        out->channel_id=old.channel_id;out->enabled=old.enabled;
        memcpy(out->read_api_key,old.read_api_key,sizeof(out->read_api_key));
        memcpy(out->write_api_key,old.write_api_key,sizeof(out->write_api_key));
        out->read_api_key[sizeof(out->read_api_key)-1]=0;
        out->write_api_key[sizeof(out->write_api_key)-1]=0;
    }else if(err==ESP_ERR_NVS_NOT_FOUND) {
        read_string(h,"read_key",out->read_api_key,sizeof(out->read_api_key));
        read_string(h,"write_key",out->write_api_key,sizeof(out->write_api_key));
        nvs_get_u32(h,"channel_id",&out->channel_id);
        uint8_t enabled=0;nvs_get_u8(h,"enabled",&enabled);out->enabled=enabled==1;
    }else{nvs_close(h);return err==ESP_OK?ESP_ERR_INVALID_SIZE:err;}
    nvs_close(h);
    if(out->channel_id) snprintf(out->channel_url,sizeof(out->channel_url),
        "https://thingspeak.mathworks.com/channels/%lu",(unsigned long)out->channel_id);
    return ESP_OK;
}

esp_err_t thingspeak_config_save(const thingspeak_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    thingspeak_config_t merged = *config, previous;
    esp_err_t err = thingspeak_config_get(&previous);
    if (err != ESP_OK) return err;
    if (!merged.read_api_key[0]) strcpy(merged.read_api_key, previous.read_api_key);
    if (!merged.write_api_key[0]) strcpy(merged.write_api_key, previous.write_api_key);
    if (!safe_api_key(merged.read_api_key) || !safe_api_key(merged.write_api_key))
        return ESP_ERR_INVALID_ARG;
    if (merged.enabled && (!merged.channel_id || !merged.read_api_key[0] || !merged.write_api_key[0]))
        return ESP_ERR_INVALID_ARG;
    merged.channel_url[0] = '\0';
    if (merged.channel_id) snprintf(merged.channel_url, sizeof(merged.channel_url),
        "https://thingspeak.mathworks.com/channels/%lu", (unsigned long)merged.channel_id);
    nvs_handle_t h;
    err = nvs_open(THINGSPEAK_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    /* One NVS value avoids readers observing a partially changed configuration. */
    err = nvs_set_blob(h, "journal_config", &merged, sizeof(merged));
    if(err==ESP_OK) {
        const char *obsolete[]={"cloud_config","login","password","commands","command_chan","result_chan","mqtt_client"};
        for(unsigned i=0;i<sizeof(obsolete)/sizeof(obsolete[0]);i++) {
            esp_err_t removed=nvs_erase_key(h,obsolete[i]);
            if(removed!=ESP_OK&&removed!=ESP_ERR_NVS_NOT_FOUND) {err=removed;break;}
        }
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
