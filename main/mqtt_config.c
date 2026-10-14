#include "mqtt_config.h"
#include <stdio.h>
#include <string.h>
#include "esp_mac.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"

static portMUX_TYPE s_config_cache_lock = portMUX_INITIALIZER_UNLOCKED;
static mqtt_config_t s_config_cache;
static uint32_t s_config_cache_generation;
static bool s_config_cache_valid;

static esp_err_t identity(mqtt_config_t *cfg)
{
    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) return err;
    snprintf(cfg->client_id,sizeof(cfg->client_id),"lock-%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    snprintf(cfg->prefix,sizeof(cfg->prefix),"j%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    return ESP_OK;
}
static esp_err_t load_config(mqtt_config_t *out)
{
    memset(out,0,sizeof(*out)); out->version=2;
    nvs_handle_t h;
    esp_err_t err=nvs_open("mqtt",NVS_READONLY,&h);
    if (err==ESP_OK) {
        size_t size=sizeof(*out);
        err=nvs_get_blob(h,"public",out,&size); nvs_close(h);
        if (err==ESP_OK && (size!=sizeof(*out)||out->version!=2)) return ESP_ERR_INVALID_STATE;
    }
    if (err!=ESP_OK && err!=ESP_ERR_NVS_NOT_FOUND) return err;
    out->reserved_port=0;
    if (!out->host[0]) strcpy(out->host,"broker.emqx.io");
    err=identity(out);
    return err==ESP_OK ? mqtt_config_validate(out) : err;
}

esp_err_t mqtt_config_get(mqtt_config_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    for (;;) {
        portENTER_CRITICAL(&s_config_cache_lock);
        if (s_config_cache_valid) {
            *out = s_config_cache;
            portEXIT_CRITICAL(&s_config_cache_lock);
            return ESP_OK;
        }
        uint32_t generation = s_config_cache_generation;
        portEXIT_CRITICAL(&s_config_cache_lock);

        mqtt_config_t loaded;
        esp_err_t err = load_config(&loaded);
        if (err != ESP_OK) return err;

        portENTER_CRITICAL(&s_config_cache_lock);
        if (generation == s_config_cache_generation) {
            s_config_cache = loaded;
            s_config_cache_valid = true;
            *out = loaded;
            portEXIT_CRITICAL(&s_config_cache_lock);
            return ESP_OK;
        }
        if (s_config_cache_valid) {
            *out = s_config_cache;
            portEXIT_CRITICAL(&s_config_cache_lock);
            return ESP_OK;
        }
        portEXIT_CRITICAL(&s_config_cache_lock);
    }
}
esp_err_t mqtt_config_validate(const mqtt_config_t *cfg)
{
    if (!cfg||cfg->version!=2||!memchr(cfg->host,0,sizeof(cfg->host))||
        (cfg->enabled&&!cfg->host[0])) return ESP_ERR_INVALID_ARG;
    for(const char *p=cfg->host;*p;++p)
        if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='.'||*p=='-')) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}
esp_err_t mqtt_config_save(const mqtt_config_t *config)
{
    esp_err_t err=mqtt_config_validate(config);
    if(err!=ESP_OK) return err;
    mqtt_config_t cfg=*config;
    cfg.reserved_port=0;
    err=identity(&cfg); if(err!=ESP_OK) return err;
    nvs_handle_t h; err=nvs_open("mqtt",NVS_READWRITE,&h);
    if(err==ESP_OK) {
        err=nvs_set_blob(h,"public",&cfg,sizeof(cfg));
        if(err==ESP_OK) {
            esp_err_t removed=nvs_erase_key(h,"config");
            if(removed!=ESP_OK&&removed!=ESP_ERR_NVS_NOT_FOUND) err=removed;
        }
        if(err==ESP_OK) err=nvs_commit(h);
        nvs_close(h);
    }
    if (err == ESP_OK) {
        portENTER_CRITICAL(&s_config_cache_lock);
        s_config_cache_valid = false;
        s_config_cache_generation++;
        portEXIT_CRITICAL(&s_config_cache_lock);
    }
    return err;
}
