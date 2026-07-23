#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#define DIAG_SSID     "ESP-DIAG"
#define DIAG_PASSWORD "123456798"
#define DIAG_CHANNEL  1

static const char *TAG = "wifi_diag";

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;
    if (base != WIFI_EVENT) return;

    if (event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "AP_START");
    } else if (event_id == WIFI_EVENT_AP_STOP) {
        ESP_LOGI(TAG, "AP_STOP");
    } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "client connected");
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "client disconnected");
    }
}

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS");
        err = nvs_flash_init();
    }
    return err;
}

void app_main(void)
{
    ESP_ERROR_CHECK(init_nvs());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *ap = esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(ap ? ESP_OK : ESP_ERR_NO_MEM);

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                wifi_event_handler, NULL));

    wifi_config_t cfg = {0};
    snprintf((char *)cfg.ap.ssid, sizeof(cfg.ap.ssid), "%s", DIAG_SSID);
    snprintf((char *)cfg.ap.password, sizeof(cfg.ap.password), "%s",
             DIAG_PASSWORD);
    cfg.ap.ssid_len = strlen(DIAG_SSID);
    cfg.ap.channel = DIAG_CHANNEL;
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.ap.max_connection = 4;
    cfg.ap.ssid_hidden = 0;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_config_t active = {0};
    uint8_t mac[6];
    uint8_t primary = 0;
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
    ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_AP, &active));
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_AP, mac));
    ESP_ERROR_CHECK(esp_wifi_get_channel(&primary, &secondary));

    ESP_LOGI(TAG,
             "READY SSID='%.*s' password='%s' BSSID=" MACSTR
             " channel=%u auth=%u hidden=%u",
             active.ap.ssid_len, active.ap.ssid, DIAG_PASSWORD,
             MAC2STR(mac), primary, active.ap.authmode,
             active.ap.ssid_hidden);
}
