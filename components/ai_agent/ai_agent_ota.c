#include "ai_agent_internal.h"
#include <stdatomic.h>
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static atomic_flag updating = ATOMIC_FLAG_INIT;
static TaskHandle_t restart_task_handle;
static void restart_task(void *arg)
{
    (void)arg;
    /* Wait until the response send has completed before touching the network. */
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (ai_agent_config.prepare_restart) ai_agent_config.prepare_restart(ai_agent_config.ctx);
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_LOGI("agent_restart", "stack low-water: %u bytes", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    esp_restart();
}
static esp_err_t reserve_restart(void)
{
    return xTaskCreate(restart_task, "agent_restart", 4096, NULL, 5,
                       &restart_task_handle) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
esp_err_t ai_agent_http_restart(httpd_req_t *req)
{
    if (ai_agent_http_guard(req, "restart") != ESP_OK) return ESP_OK;
    if (atomic_flag_test_and_set(&updating))
        return ai_agent_http_error(req, "409 Conflict", "Restart or OTA in progress");
    if (reserve_restart() != ESP_OK) {
        atomic_flag_clear(&updating);
        return ai_agent_http_error(req, "500 Internal Server Error", "Restart task unavailable");
    }
    esp_err_t err = ai_agent_http_json(req, "{\"ok\":true,\"accepted\":true}");
    xTaskNotifyGive(restart_task_handle);
    return err;
}
esp_err_t ai_agent_http_ota(httpd_req_t *req)
{
    if (ai_agent_http_guard(req, "ota") != ESP_OK) return ESP_OK;
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (!partition) return ai_agent_http_error(req, "501 Not Implemented", "No OTA partition");
    if (req->content_len <= 0 || (size_t)req->content_len > partition->size)
        return ai_agent_http_error(req, "400 Bad Request", "Invalid firmware size");
    if (atomic_flag_test_and_set(&updating))
        return ai_agent_http_error(req, "409 Conflict", "Restart or OTA in progress");
    esp_ota_handle_t handle;
    esp_err_t err = esp_ota_begin(partition, req->content_len, &handle);
    if (err != ESP_OK) goto failed;
    char buffer[2048];
    int remaining = req->content_len;
    unsigned timeouts = 0;
    while (remaining > 0) {
        int received = httpd_req_recv(req, buffer, remaining > (int)sizeof(buffer) ? sizeof(buffer) : remaining);
        if (received == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3) continue;
        if (received <= 0) { err = ESP_ERR_TIMEOUT; esp_ota_abort(handle); goto failed; }
        timeouts = 0;
        err = esp_ota_write(handle, buffer, received);
        if (err != ESP_OK) { esp_ota_abort(handle); goto failed; }
        remaining -= received;
    }
    err = esp_ota_end(handle);
    if (err != ESP_OK) goto failed;
    err = reserve_restart();
    if (err != ESP_OK) goto failed;
    err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) { vTaskDelete(restart_task_handle); restart_task_handle = NULL; goto failed; }
    if (ai_agent_config.ota_complete) ai_agent_config.ota_complete(ai_agent_config.ctx);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    err = httpd_resp_sendstr(req, "Прошивка записана");
    xTaskNotifyGive(restart_task_handle);
    return err;
failed:
    atomic_flag_clear(&updating);
    return ai_agent_http_error(req, "500 Internal Server Error", esp_err_to_name(err));
}
