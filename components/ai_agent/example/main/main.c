#include <stdatomic.h>
#include <stdio.h>
#include "ai_agent.h"
#include "ai_agent_http.h"
#include "esp_event.h"
#include "esp_netif.h"

/* Demonstration state only. Real hardware callbacks should queue work to the
 * application's relay task and report queue errors to the dispatcher. */
static atomic_bool relay_enabled;
static esp_err_t relay_on(const ai_agent_args_t *args, void *ctx)
{
    (void)args;
    atomic_store((atomic_bool *)ctx, true);
    return ESP_OK;
}
static esp_err_t relay_off(const ai_agent_args_t *args, void *ctx)
{
    (void)args;
    atomic_store((atomic_bool *)ctx, false);
    return ESP_OK;
}
static esp_err_t relay_status(char *out, size_t capacity, void *ctx)
{
    int n = snprintf(out, capacity, "{\"enabled\":%s}",
                     atomic_load((atomic_bool *)ctx) ? "true" : "false");
    return n >= 0 && (size_t)n < capacity ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

void app_main(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    /* Connect your application's Wi-Fi/Ethernet here. ai_agent owns no network. */
    ai_agent_config_t cfg = {
        .device_type = "relay_controller", .device_name = "Relay Controller",
        .firmware_version = "1.0.0", .device_id = "relay-demo-001",
        .history_url = NULL, .status = relay_status, .ctx = &relay_enabled,
    };
    ESP_ERROR_CHECK(ai_agent_init(&cfg));
    const ai_agent_command_t on = {"relay_on", "Enable relay", relay_on, &relay_enabled};
    const ai_agent_command_t off = {"relay_off", "Disable relay", relay_off, &relay_enabled};
    ESP_ERROR_CHECK(ai_agent_register_command(&on));
    ESP_ERROR_CHECK(ai_agent_register_command(&off));
    ESP_ERROR_CHECK(ai_agent_seal());
    httpd_config_t http = HTTPD_DEFAULT_CONFIG();
    http.max_uri_handlers = 8; /* Plus your application's own routes, if any. */
    http.stack_size = 6144;
    httpd_handle_t server;
    ESP_ERROR_CHECK(httpd_start(&server, &http));
    ESP_ERROR_CHECK(ai_agent_register_http_handlers(server));
}
