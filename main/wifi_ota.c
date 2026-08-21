#include "wifi_ota.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "web_ui.h"

#define WIFI_OTA_NVS_NS "wifi_ota"
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT BIT1
#define WIFI_OTA_STATUS_MAX 32

static const char *TAG = "wifi_ota";
static EventGroupHandle_t s_wifi_events;
static bool s_initialized;
static bool s_task_running;
static bool s_wifi_stack_ready;
static bool s_wifi_connected;
static bool s_web_active;
static bool s_web_task_running;
static bool s_auto_connect = true;
static esp_netif_t *s_sta_netif;
static char s_ssid[WIFI_OTA_SSID_MAX + 1];
static char s_password[WIFI_OTA_PASSWORD_MAX + 1];
static char s_url[WIFI_OTA_URL_MAX + 1];
static char s_status[WIFI_OTA_STATUS_MAX];

static void stop_current_web_connection(void);

static void set_status(const char *status)
{
    snprintf(s_status, sizeof(s_status), "%s", status);
    ESP_LOGI(TAG, "OTA status: %s", s_status);
}

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    if (!event) return ESP_OK;
    switch (event->event_id) {
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTPS connected");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTPS header: %s: %s",
                     event->header_key ? event->header_key : "?",
                     event->header_value ? event->header_value : "?");
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTPS data chunk: %d bytes", event->data_len);
            if (event->data_len > 0 && strcmp(s_status, "DOWNLOADING") != 0) {
                set_status("DOWNLOADING");
            }
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTPS disconnected");
            break;
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTPS transport error");
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        set_status("ERROR_WIFI");
        /* A disconnect event can belong to the previous connection when
         * runtime credentials replace an already running Web UI.  Do not
         * let that stale event abort the next connection attempt. */
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_connected = true;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t load_string(nvs_handle_t handle, const char *key,
                             char *out, size_t out_size)
{
    size_t length = out_size;
    esp_err_t err = nvs_get_str(handle, key, out, &length);
    if (err == ESP_ERR_NVS_NOT_FOUND) out[0] = '\0';
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

esp_err_t wifi_ota_init(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_OTA_NVS_NS, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        load_string(handle, "ssid", s_ssid, sizeof(s_ssid));
        load_string(handle, "password", s_password, sizeof(s_password));
        load_string(handle, "url", s_url, sizeof(s_url));
        uint8_t auto_connect = 1;
        if (nvs_get_u8(handle, "auto_connect", &auto_connect) == ESP_OK) {
            s_auto_connect = auto_connect != 0;
        }
        nvs_close(handle);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }
    /* Сетевой статус является только текущим состоянием, а не настройкой.
     * Удаляем старый ключ, оставшийся от предыдущих версий прошивки, чтобы
     * прежний WEB_READY;IP не мог быть принят за действующее подключение. */
    if (nvs_open(WIFI_OTA_NVS_NS, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_key(handle, "status");
        nvs_commit(handle);
        nvs_close(handle);
    }
    s_wifi_events = xEventGroupCreate();
    if (!s_wifi_events) return ESP_ERR_NO_MEM;
    snprintf(s_status, sizeof(s_status), "IDLE");
    s_initialized = true;
    ESP_LOGI(TAG, "initialized; Wi-Fi config %s, URL %s",
             s_ssid[0] && s_password[0] ? "present" : "incomplete",
             s_url[0] ? "present" : "missing");
    return ESP_OK;
}

esp_err_t wifi_ota_set_config(const char *payload, size_t length)
{
    if (!payload || length == 0 || length > WIFI_OTA_SSID_MAX + 1 +
        WIFI_OTA_PASSWORD_MAX + 1 + WIFI_OTA_URL_MAX + 1) {
        return ESP_ERR_INVALID_ARG;
    }
    char copy[WIFI_OTA_SSID_MAX + WIFI_OTA_PASSWORD_MAX + WIFI_OTA_URL_MAX + 4];
    memcpy(copy, payload, length);
    copy[length] = '\0';
    char *separator = strchr(copy, '\n');
    char *password = separator ? separator + 1 : NULL;
    char *url = password ? strchr(password, '\n') : NULL;
    if (!separator || !url) {
        ESP_LOGE(TAG, "configuration rejected: expected SSID\\npassword\\nHTTPS_URL");
        return ESP_ERR_INVALID_ARG;
    }
    *separator = '\0';
    *url = '\0';
    url++;
    if (copy[0] == '\0') {
        ESP_LOGE(TAG, "configuration rejected: SSID is empty");
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(copy) > WIFI_OTA_SSID_MAX) {
        ESP_LOGE(TAG, "configuration rejected: SSID is too long (%u)", (unsigned)strlen(copy));
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(password) > WIFI_OTA_PASSWORD_MAX) {
        ESP_LOGE(TAG, "configuration rejected: password is too long (%u)", (unsigned)strlen(password));
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(url) > WIFI_OTA_URL_MAX || strncmp(url, "https://", 8) != 0) {
        ESP_LOGE(TAG, "configuration rejected: OTA URL must start with https:// and be <= %d chars",
                 WIFI_OTA_URL_MAX);
        return ESP_ERR_INVALID_ARG;
    }
    const char *password_to_save = password[0] ? password : s_password;
    if (password_to_save[0] == '\0') {
        ESP_LOGE(TAG, "configuration rejected: password is empty and no saved password exists");
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_OTA_NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, "ssid", copy);
    if (err == ESP_OK) err = nvs_set_str(handle, "password", password_to_save);
    if (err == ESP_OK) err = nvs_set_str(handle, "url", url);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) {
        strcpy(s_ssid, copy);
        strcpy(s_password, password_to_save);
        strcpy(s_url, url);
        ESP_LOGI(TAG, "configuration saved: SSID='%s', URL='%s'", s_ssid, s_url);
    } else {
        ESP_LOGE(TAG, "configuration NVS write failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t wifi_ota_set_runtime_config(const char *payload, size_t length)
{
    if (!payload || length == 0 || length > WIFI_OTA_SSID_MAX + WIFI_OTA_PASSWORD_MAX + 2) {
        return ESP_ERR_INVALID_ARG;
    }
    char copy[WIFI_OTA_SSID_MAX + WIFI_OTA_PASSWORD_MAX + 2];
    memcpy(copy, payload, length);
    copy[length] = '\0';
    char *separator = strchr(copy, '\n');
    if (!separator) return ESP_ERR_INVALID_ARG;
    *separator = '\0';
    char *password = separator + 1;
    if (copy[0] == '\0' || strlen(copy) > WIFI_OTA_SSID_MAX ||
        strlen(password) > WIFI_OTA_PASSWORD_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    stop_current_web_connection();
    strcpy(s_ssid, copy);
    strcpy(s_password, password);
    ESP_LOGI(TAG, "runtime Wi-Fi configuration received over BLE: SSID='%s' (not saved)",
             s_ssid);
    return wifi_ota_start_runtime_web();
}

esp_err_t wifi_ota_get_config(char *out, size_t out_size)
{
    if (!out || out_size == 0) return ESP_ERR_INVALID_ARG;
    /* Never expose the Wi-Fi password over BLE. */
    int written = snprintf(out, out_size, "%s\n%s", s_ssid, s_url);
    return written < 0 || (size_t)written >= out_size ? ESP_ERR_NO_MEM : ESP_OK;
}

esp_err_t wifi_ota_get_web_config(char *out, size_t out_size)
{
    if (!out || out_size == 0) return ESP_ERR_INVALID_ARG;
    /* Do not expose the saved password in the browser. An empty password
     * submitted by the Web UI means keep the current password. */
    int written = snprintf(out, out_size, "%s\n\n%s", s_ssid, s_url);
    return written < 0 || (size_t)written >= out_size ? ESP_ERR_NO_MEM : ESP_OK;
}

bool wifi_ota_password_configured(void)
{
    return s_password[0] != '\0';
}

bool wifi_ota_config_matches(const char *ssid, const char *url)
{
    return ssid && url && strcmp(s_ssid, ssid) == 0 && strcmp(s_url, url) == 0;
}

esp_err_t wifi_ota_get_status(char *out, size_t out_size)
{
    if (!out || out_size == 0) return ESP_ERR_INVALID_ARG;
    int written = snprintf(out, out_size, "%s", s_status[0] ? s_status : "IDLE");
    return written < 0 || (size_t)written >= out_size ? ESP_ERR_NO_MEM : ESP_OK;
}

bool wifi_ota_get_auto_connect(void)
{
    return s_auto_connect;
}

esp_err_t wifi_ota_set_auto_connect(bool enabled)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_OTA_NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, "auto_connect", enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) s_auto_connect = enabled;
    return err;
}

static esp_err_t ensure_wifi_stack(void)
{
    if (s_wifi_stack_ready) return ESP_OK;
    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) return ESP_ERR_NO_MEM;
    err = esp_wifi_init(&wifi_init);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                     wifi_event_handler, NULL);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                     wifi_event_handler, NULL);
    if (err != ESP_OK) return err;
    s_wifi_stack_ready = true;
    return ESP_OK;
}

static esp_err_t connect_wifi(void)
{
    esp_err_t err = ensure_wifi_stack();
    if (err != ESP_OK) return err;
    if (s_wifi_connected) return ESP_OK;
    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT);
    wifi_config_t config = {0};
    strcpy((char *)config.sta.ssid, s_ssid);
    strcpy((char *)config.sta.password, s_password);
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    /* Wait for the IP event only.  WIFI_EVENT_STA_DISCONNECTED may be a
     * delayed notification from the connection that was just stopped. */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                           WIFI_CONNECTED_BIT,
                                           pdTRUE, pdFALSE, pdMS_TO_TICKS(30000));
    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void stop_current_web_connection(void)
{
    if (s_web_active) {
        web_ui_stop();
        s_web_active = false;
    }
    if (s_wifi_stack_ready) {
        esp_wifi_disconnect();
        esp_wifi_stop();
    }
    s_wifi_connected = false;
}

static void web_task(void *arg)
{
    (void)arg;
    set_status("CONNECTING_WEB");
    esp_err_t err = connect_wifi();
    if (err == ESP_OK) {
        err = web_ui_start();
        if (err == ESP_OK) {
            s_web_active = true;
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(s_sta_netif, &ip_info) == ESP_OK) {
                char status[WIFI_OTA_STATUS_MAX];
                snprintf(status, sizeof(status), "WEB_READY;IP=" IPSTR, IP2STR(&ip_info.ip));
                set_status(status);
            } else {
                set_status("WEB_READY");
            }
        } else {
            set_status("ERROR_WEB");
            ESP_LOGE(TAG, "Web UI start failed: %s", esp_err_to_name(err));
        }
    } else {
        set_status("ERROR_WIFI");
        ESP_LOGE(TAG, "Web Wi-Fi connection failed: %s", esp_err_to_name(err));
    }
    s_web_task_running = false;
    vTaskDelete(NULL);
}

static esp_err_t start_web_task(bool respect_auto_connect)
{
    if (!s_initialized || (respect_auto_connect && !s_auto_connect) ||
        s_ssid[0] == '\0' || s_password[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_web_active || s_web_task_running) return ESP_OK;
    s_web_task_running = true;
    if (xTaskCreate(web_task, "wifi_web", 6144, NULL, 4, NULL) != pdPASS) {
        s_web_task_running = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t wifi_ota_start_web(void)
{
    return start_web_task(true);
}

esp_err_t wifi_ota_start_runtime_web(void)
{
    return start_web_task(false);
}

static void ota_task(void *arg)
{
    (void)arg;
    set_status("CONNECTING_WIFI");
    ESP_LOGI(TAG, "OTA task started; target URL='%s'", s_url);
    esp_err_t err = ESP_OK;
    err = connect_wifi();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi connection failed: %s", esp_err_to_name(err));
        set_status("ERROR_WIFI");
        goto done;
    }
    ESP_LOGI(TAG, "Wi-Fi connected, starting HTTPS OTA");
    esp_http_client_config_t http = {
        .url = s_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000,
        .event_handler = http_event_handler,
        /* GitHub Release assets use a long signed Location redirect. */
        .buffer_size = 8192,
        .buffer_size_tx = 4096,
    };
    esp_https_ota_config_t ota = {.http_config = &http};
    err = esp_https_ota(&ota);
    if (err == ESP_OK) {
        set_status("SUCCESS");
        ESP_LOGI(TAG, "OTA image downloaded and verified, restarting");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    } else {
        set_status("ERROR_OTA");
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
    }
done:
    if (s_wifi_stack_ready && !s_web_active) {
        esp_err_t stop_err = esp_wifi_stop();
        ESP_LOGI(TAG, "Wi-Fi stop: %s", esp_err_to_name(stop_err));
    }
    s_task_running = false;
    vTaskDelete(NULL);
}

esp_err_t wifi_ota_start(void)
{
    if (!s_initialized || s_ssid[0] == '\0' || s_password[0] == '\0' ||
        s_url[0] == '\0') {
        ESP_LOGE(TAG, "OTA start rejected: configuration incomplete");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_task_running) {
        ESP_LOGW(TAG, "OTA start ignored: task already running");
        return ESP_ERR_INVALID_STATE;
    }
    set_status("STARTING");
    s_task_running = true;
    BaseType_t task_result = xTaskCreate(ota_task, "wifi_ota", 8192, NULL, 5, NULL);
    if (task_result != pdPASS) {
        s_task_running = false;
        ESP_LOGE(TAG, "OTA task creation failed");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "OTA task scheduled");
    return ESP_OK;
}
