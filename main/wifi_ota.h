#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define WIFI_OTA_SSID_MAX 32
#define WIFI_OTA_PASSWORD_MAX 64
#define WIFI_OTA_URL_MAX 192

esp_err_t wifi_ota_init(void);
esp_err_t wifi_ota_set_config(const char *payload, size_t length);
esp_err_t wifi_ota_set_runtime_config(const char *payload, size_t length);
/* Phone hotspot IPv4 in network byte order; NULL clears it. RAM-only session state. */
esp_err_t wifi_ota_set_runtime_phone_ip(const uint8_t address[4]);
bool wifi_ota_get_runtime_phone_ip(uint8_t address_out[4]);
bool wifi_ota_is_runtime_phone_session(void);
esp_err_t wifi_ota_get_config(char *out, size_t out_size);
esp_err_t wifi_ota_get_web_config(char *out, size_t out_size);
bool wifi_ota_password_configured(void);
bool wifi_ota_config_matches(const char *ssid, const char *url);
esp_err_t wifi_ota_get_status(char *out, size_t out_size);
bool wifi_ota_get_auto_connect(void);
/* MQTT requires saved home Wi-Fi; an explicit phone Web UI session stays temporary. */
void wifi_ota_set_mqtt_required(bool enabled);
esp_err_t wifi_ota_request_home_wifi(void);
esp_err_t wifi_ota_set_auto_connect(bool enabled);
bool wifi_ota_get_auto_update(void);
/* Legacy API: enabling direct downloads is unsupported; OTA comes from the phone. */
esp_err_t wifi_ota_set_auto_update(bool enabled);
esp_err_t wifi_ota_start(void);
esp_err_t wifi_ota_check_for_update(void);
esp_err_t wifi_ota_start_web(void);
esp_err_t wifi_ota_start_runtime_web(void);
void wifi_ota_prepare_restart(void);
