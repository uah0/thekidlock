#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define WIFI_OTA_SSID_MAX 32
#define WIFI_OTA_PASSWORD_MAX 64
#define WIFI_OTA_URL_MAX 192

esp_err_t wifi_ota_init(void);
esp_err_t wifi_ota_set_config(const char *payload, size_t length);
esp_err_t wifi_ota_set_runtime_config(const char *payload, size_t length);
esp_err_t wifi_ota_get_config(char *out, size_t out_size);
esp_err_t wifi_ota_get_web_config(char *out, size_t out_size);
bool wifi_ota_password_configured(void);
bool wifi_ota_config_matches(const char *ssid, const char *url);
esp_err_t wifi_ota_get_status(char *out, size_t out_size);
bool wifi_ota_get_auto_connect(void);
esp_err_t wifi_ota_set_auto_connect(bool enabled);
esp_err_t wifi_ota_start(void);
esp_err_t wifi_ota_start_web(void);
esp_err_t wifi_ota_start_runtime_web(void);
