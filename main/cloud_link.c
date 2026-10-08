#include "cloud_link.h"
#include "cloud_commands.h"

#include <stdatomic.h>
#include <sys/time.h>
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static atomic_bool s_time_ready;

bool cloud_link_time_ready(void)
{
    return atomic_load(&s_time_ready);
}

static void time_synced(struct timeval *tv)
{
    (void)tv;
    atomic_store(&s_time_ready, true);
}

static bool network_ready(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info;
    return netif && esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr;
}

static void clock_task(void *arg)
{
    (void)arg;
    bool sntp_started = false;
    for (;;) {
        if ((cloud_commands_time_required() || cloud_commands_connected()) &&
            network_ready() && !sntp_started) {
            esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("time.cloudflare.com");
            config.sync_cb = time_synced;
            sntp_started = esp_netif_sntp_init(&config) == ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t cloud_link_init(void)
{
    return cloud_commands_init();
}

esp_err_t cloud_link_start(void)
{
    esp_err_t err = cloud_commands_start();
    if (err != ESP_OK) return err;
    return xTaskCreate(clock_task, "cloud_clock", 3072, NULL, 2, NULL) == pdPASS ?
        ESP_OK : ESP_ERR_NO_MEM;
}
