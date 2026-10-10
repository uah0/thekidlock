#include "wifi_ota.h"
#include "mqtt_config.h"
#include "cloud_commands.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "esp_timer.h"
#include "event_log.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "web_ui.h"

#define WIFI_OTA_NVS_NS "wifi_ota"
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT BIT1
#define WIFI_OTA_STATUS_MAX 32

static const char *TAG = "wifi_ota";
static EventGroupHandle_t s_wifi_events;
/* State/config operations are short; never hold this mutex while awaiting IP. */
static SemaphoreHandle_t s_state_mutex;
static bool s_initialized;
static bool s_task_running;
static bool s_wifi_stack_ready;
static bool s_wifi_connected;
static bool s_web_active;
static bool s_web_task_running;
static bool s_reconnect_enabled;
static bool s_auto_connect = true;
static atomic_bool s_mqtt_required;
static int64_t s_mqtt_wifi_retry_us;
static esp_netif_t *s_sta_netif;
static char s_ssid[WIFI_OTA_SSID_MAX + 1];
static char s_password[WIFI_OTA_PASSWORD_MAX + 1];
static char s_url[WIFI_OTA_URL_MAX + 1];
static char s_saved_ssid[WIFI_OTA_SSID_MAX + 1];
static char s_saved_password[WIFI_OTA_PASSWORD_MAX + 1];
static char s_saved_url[WIFI_OTA_URL_MAX + 1];
static char s_status[WIFI_OTA_STATUS_MAX];
/* Diagnostics only: never used to decide connection policy. */
static atomic_uint s_connect_attempts;
static atomic_uint s_last_disconnect_reason;
static atomic_uint s_last_disconnect_ms;
static atomic_bool s_runtime_source;
static uint8_t s_runtime_phone_ip[4];

static const char *wifi_reason_name(unsigned reason)
{
    switch (reason) {
        case 0: return "NONE_RECORDED";
        case WIFI_REASON_AUTH_EXPIRE: return "AUTH_EXPIRE";
        case WIFI_REASON_AUTH_LEAVE: return "AUTH_LEAVE";
        case WIFI_REASON_ASSOC_LEAVE: return "ASSOC_LEAVE";
        case WIFI_REASON_ASSOC_TOOMANY: return "ASSOC_TOOMANY";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_HANDSHAKE_TIMEOUT";
        case WIFI_REASON_802_1X_AUTH_FAILED: return "802_1X_AUTH_FAILED";
        case WIFI_REASON_BEACON_TIMEOUT: return "BEACON_TIMEOUT";
        case WIFI_REASON_NO_AP_FOUND: return "NO_AP_FOUND";
        case WIFI_REASON_AUTH_FAIL: return "AUTH_FAIL";
        case WIFI_REASON_ASSOC_FAIL: return "ASSOC_FAIL";
        case WIFI_REASON_HANDSHAKE_TIMEOUT: return "HANDSHAKE_TIMEOUT";
        case WIFI_REASON_CONNECTION_FAIL: return "CONNECTION_FAIL";
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY: return "NO_COMPATIBLE_SECURITY";
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD: return "AUTHMODE_THRESHOLD";
        case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD: return "RSSI_THRESHOLD";
        default: return "OTHER_SEE_IDF_REASON_CODE";
    }
}

static const char *wifi_source(void)
{
    return atomic_load(&s_runtime_source) ? "phone_runtime" : "saved";
}

static bool home_auto_connect(void)
{
    return s_auto_connect || atomic_load(&s_mqtt_required);
}

static esp_err_t connect_with_diagnostics_locked(const char *trigger);
static esp_err_t connect_with_diagnostics(const char *trigger)
{
    if (!s_state_mutex) { return ESP_ERR_INVALID_STATE; }
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    esp_err_t result = connect_with_diagnostics_locked(trigger);
    xSemaphoreGiveRecursive(s_state_mutex);
    return result;
}
static esp_err_t connect_with_diagnostics_locked(const char *trigger)
{
    unsigned attempt = atomic_fetch_add(&s_connect_attempts, 1) + 1;
    esp_err_t err = esp_wifi_connect();
    ESP_LOGI(TAG, "WiFiDiag attempt=%u trigger=%s source=%s target='%s' result=%s",
             attempt, trigger, wifi_source(), s_ssid, esp_err_to_name(err));
    return err;
}

static void stop_current_web_connection(void);

static void activate_saved_config_locked(void);
static void activate_saved_config(void)
{
    if (!s_state_mutex) { return; }
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    activate_saved_config_locked();
    xSemaphoreGiveRecursive(s_state_mutex);

}
static void activate_saved_config_locked(void)
{
    atomic_store(&s_runtime_source, false);
    memset(s_runtime_phone_ip, 0, sizeof(s_runtime_phone_ip));
    strcpy(s_ssid, s_saved_ssid);
    strcpy(s_password, s_saved_password);
    strcpy(s_url, s_saved_url);
}

esp_err_t wifi_ota_set_runtime_phone_ip(const uint8_t address[4])
{
    if (!s_state_mutex) return ESP_ERR_INVALID_STATE;
    uint8_t value[4] = {0};
    if (address) {
        memcpy(value, address, sizeof(value));
        bool all_zero = value[0] == 0 && value[1] == 0 && value[2] == 0 && value[3] == 0;
        bool all_broadcast = value[0] == 255 && value[1] == 255 && value[2] == 255 && value[3] == 255;
        if (!all_zero && (all_broadcast || value[0] == 127 || value[0] >= 224 ||
                          (value[0] == 169 && value[1] == 254))) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    memcpy(s_runtime_phone_ip, value, sizeof(value));
    xSemaphoreGiveRecursive(s_state_mutex);
    return ESP_OK;
}

bool wifi_ota_get_runtime_phone_ip(uint8_t address_out[4])
{
    if (!address_out || !s_state_mutex) return false;
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    memcpy(address_out, s_runtime_phone_ip, sizeof(s_runtime_phone_ip));
    xSemaphoreGiveRecursive(s_state_mutex);
    return address_out[0] || address_out[1] || address_out[2] || address_out[3];
}

bool wifi_ota_is_runtime_phone_session(void)
{
    if (!s_state_mutex) return false;
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    bool result = atomic_load(&s_runtime_source);
    xSemaphoreGiveRecursive(s_state_mutex);
    return result;
}

static void set_status_locked(const char *status);
static void set_status(const char *status)
{
    if (!s_state_mutex) { return; }
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    set_status_locked(status);
    xSemaphoreGiveRecursive(s_state_mutex);

}
static void set_status_locked(const char *status)
{
    snprintf(s_status, sizeof(s_status), "%s", status);
    ESP_LOGI(TAG, "OTA status: %s", s_status);
}

static void set_web_ready_status(const esp_netif_ip_info_t *ip_info)
{
    if (ip_info) {
        char status[WIFI_OTA_STATUS_MAX];
        snprintf(status, sizeof(status), "WEB_READY;IP=" IPSTR, IP2STR(&ip_info->ip));
        set_status(status);
    } else {
        set_status("WEB_READY");
    }
}

static void wifi_event_handler_locked(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data);
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    if (!s_state_mutex) { return; }
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    wifi_event_handler_locked(arg, base, event_id, event_data);
    xSemaphoreGiveRecursive(s_state_mutex);

}
static void wifi_event_handler_locked(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_reconnect_enabled) connect_with_diagnostics("STA_START");
    } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        const wifi_event_sta_connected_t *e = event_data;
        if (e) ESP_LOGI(TAG, "WiFiDiag associated ssid='%.*s' channel=%u authmode=%u; waiting for DHCP/IP",
                       (int)e->ssid_len, (const char *)e->ssid, e->channel, (unsigned)e->authmode);
    } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *e = event_data;
        /* Disconnect notifications from a stopped station can arrive after
         * credentials have already switched to the next network. */
        if (e && e->ssid_len > 0 &&
            (e->ssid_len != strlen(s_ssid) || memcmp(e->ssid, s_ssid, e->ssid_len) != 0)) {
            ESP_LOGI(TAG, "WiFiDiag ignoring disconnect from previous network");
            return;
        }
        unsigned reason = e ? e->reason : 0;
        atomic_store(&s_last_disconnect_reason, reason);
        atomic_store(&s_last_disconnect_ms, (unsigned)(esp_timer_get_time() / 1000));
        ESP_LOGW(TAG, "WiFiDiag disconnected event_ssid='%.*s' reason=%u(%s) rssi=%d reconnect=%d source=%s target='%s' previous_status=%s",
                 e ? (int)e->ssid_len : 0, e ? (const char *)e->ssid : "",
                 reason, wifi_reason_name(reason), e ? e->rssi : 0,
                 s_reconnect_enabled, wifi_source(), s_ssid, s_status);
        s_wifi_connected = false;
        bool event_matches_phone = atomic_load(&s_runtime_source) && e && e->ssid_len > 0 &&
            e->ssid_len == strlen(s_ssid) && memcmp(e->ssid, s_ssid, e->ssid_len) == 0;
        if (event_matches_phone) {
            ESP_LOGI(TAG, "Phone hotspot disconnected; restoring saved home Wi-Fi");
            s_reconnect_enabled = false;
            esp_err_t stop_err = esp_wifi_stop();
            activate_saved_config_locked();
            xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT);
            if (home_auto_connect() && s_saved_ssid[0] && s_saved_password[0]) {
                wifi_config_t config = {0};
                memcpy(config.sta.ssid, s_ssid, strnlen(s_ssid, sizeof(config.sta.ssid)));
                memcpy(config.sta.password, s_password, strnlen(s_password, sizeof(config.sta.password)));
                esp_err_t err = stop_err;
                if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &config);
                if (err == ESP_OK) {
                    set_status("CONNECTING_WIFI");
                    s_reconnect_enabled = true;
                    /* STA_START performs the sole connection attempt after
                     * restarting the station with fresh home credentials. */
                    err = esp_wifi_start();
                }
                if (err != ESP_OK) {
                    s_reconnect_enabled = false;
                    set_status("ERROR_WIFI");
                    ESP_LOGW(TAG, "saved Wi-Fi restore failed to start: %s", esp_err_to_name(err));
                }
            } else {
                s_reconnect_enabled = false;
                set_status("IDLE");
            }
            return;
        }
        set_status("ERROR_WIFI");
        /* A disconnect event can belong to the previous connection when
         * runtime credentials replace an already running Web UI.  Do not
         * let that stale event abort the next connection attempt. */
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        if (s_reconnect_enabled) {
            esp_err_t err = connect_with_diagnostics("DISCONNECTED");
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "automatic Wi-Fi reconnect failed to start: %s",
                         esp_err_to_name(err));
            } else {
                ESP_LOGI(TAG, "automatic Wi-Fi reconnect started");
            }
        }
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = event_data;
        wifi_ap_record_t ap = {0};
        esp_err_t ap_err = esp_wifi_sta_get_ap_info(&ap);
        ESP_LOGI(TAG, "WiFiDiag GOT_IP ip=" IPSTR " source=%s actual_ssid='%.*s' rssi=%d ap_info=%s status=%s",
                 IP2STR(&e->ip_info.ip), wifi_source(), 32, (const char *)ap.ssid,
                 ap.rssi, esp_err_to_name(ap_err), s_status);
        s_wifi_connected = true;
        if (!atomic_load(&s_runtime_source)) {
            event_log_add("Wi-Fi: home IP ready");
            cloud_commands_home_ip_ready();
        }
        /* web_task only publishes readiness on its initial connection. A later
         * reconnect must refresh the BLE-visible status and IP as well. */
        if (s_web_active && !s_task_running) {
            if (atomic_load(&s_runtime_source)) set_web_ready_status(&e->ip_info);
            else set_status("WIFI_CONNECTED");
            ESP_LOGI(TAG, "WiFiDiag IP recovered: status refreshed source=%s", wifi_source());
        } else if (strcmp(s_status, "ERROR_WIFI") == 0) {
            set_status(s_task_running ? "CONNECTING_WIFI" :
                       s_web_task_running ? "CONNECTING_WEB" : "WIFI_CONNECTED");
        }
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
    if (s_initialized) return ESP_OK;
    if (!s_state_mutex) s_state_mutex = xSemaphoreCreateRecursiveMutex();
    if (!s_state_mutex) return ESP_ERR_NO_MEM;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_OTA_NVS_NS, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        load_string(handle, "ssid", s_saved_ssid, sizeof(s_saved_ssid));
        load_string(handle, "password", s_saved_password, sizeof(s_saved_password));
        load_string(handle, "url", s_saved_url, sizeof(s_saved_url));
        uint8_t auto_connect = 1;
        if (nvs_get_u8(handle, "auto_connect", &auto_connect) == ESP_OK) {
            s_auto_connect = auto_connect != 0;
        }
        /* Legacy auto_update is ignored: OTA images arrive only from the phone. */
        nvs_close(handle);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }
    mqtt_config_t mqtt;
    atomic_store(&s_mqtt_required, mqtt_config_get(&mqtt) == ESP_OK && mqtt.enabled);
    activate_saved_config();
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
    ESP_LOGI(TAG, "WiFiDiag boot saved_ssid='%s' credentials_complete=%d auto_connect=%d auto_update=%d",
             s_saved_ssid, s_saved_ssid[0] && s_saved_password[0], s_auto_connect, false);
    return ESP_OK;
}

static esp_err_t wifi_ota_set_config_locked(const char *payload, size_t length);
esp_err_t wifi_ota_set_config(const char *payload, size_t length)
{
    if (!s_state_mutex) { return ESP_ERR_INVALID_STATE; }
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    esp_err_t result = wifi_ota_set_config_locked(payload, length);
    xSemaphoreGiveRecursive(s_state_mutex);
    return result;
}
static esp_err_t wifi_ota_set_config_locked(const char *payload, size_t length)
{
    if (s_task_running || s_web_task_running) return ESP_ERR_INVALID_STATE;
    if (!payload || length == 0 || length > WIFI_OTA_SSID_MAX + 1 +
        WIFI_OTA_PASSWORD_MAX + 1 + WIFI_OTA_URL_MAX + 1 || memchr(payload, 0, length)) {
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
    if (strlen(url) > WIFI_OTA_URL_MAX ||
        (url[0] != '\0' && strncmp(url, "https://", 8) != 0)) {
        ESP_LOGE(TAG, "configuration rejected: OTA URL must be empty or start with https:// and be <= %d chars",
                 WIFI_OTA_URL_MAX);
        return ESP_ERR_INVALID_ARG;
    }
    const char *password_to_save = password[0] ? password : s_saved_password;
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
        strcpy(s_saved_ssid, copy);
        strcpy(s_saved_password, password_to_save);
        strcpy(s_saved_url, url);
        activate_saved_config();
        ESP_LOGI(TAG, "Wi-Fi/OTA configuration saved");
    } else {
        ESP_LOGE(TAG, "configuration NVS write failed: %s", esp_err_to_name(err));
    }
    return err;
}

static esp_err_t wifi_ota_set_runtime_config_locked(const char *payload, size_t length);
esp_err_t wifi_ota_set_runtime_config(const char *payload, size_t length)
{
    if (!s_state_mutex) { return ESP_ERR_INVALID_STATE; }
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    esp_err_t result = wifi_ota_set_runtime_config_locked(payload, length);
    xSemaphoreGiveRecursive(s_state_mutex);
    return result;
}
static esp_err_t wifi_ota_set_runtime_config_locked(const char *payload, size_t length)
{
    if (s_task_running || s_web_task_running) return ESP_ERR_INVALID_STATE;
    ESP_LOGI(TAG, "WiFiDiag phone_request saved_ssid='%s' auto_connect=%d credentials_complete=%d status=%s connected=%d web_active=%d web_task=%d",
             s_saved_ssid, s_auto_connect, s_saved_ssid[0] && s_saved_password[0],
             s_status, s_wifi_connected, s_web_active, s_web_task_running);
    wifi_ap_record_t current_ap = {0};
    esp_err_t ap_err = s_wifi_stack_ready ? esp_wifi_sta_get_ap_info(&current_ap) : ESP_ERR_INVALID_STATE;
    if (!payload || length == 0 || length > WIFI_OTA_SSID_MAX + WIFI_OTA_PASSWORD_MAX + 1 || memchr(payload, 0, length)) {
        ESP_LOGW(TAG, "WiFiDiag phone rejected: invalid payload length");
        return ESP_ERR_INVALID_ARG;
    }
    char copy[WIFI_OTA_SSID_MAX + WIFI_OTA_PASSWORD_MAX + 2];
    memcpy(copy, payload, length);
    copy[length] = '\0';
    char *separator = strchr(copy, '\n');
    if (!separator) {
        ESP_LOGW(TAG, "WiFiDiag phone rejected: missing credentials separator");
        return ESP_ERR_INVALID_ARG;
    }
    *separator = '\0';
    char *password = separator + 1;
    if (copy[0] == '\0' || strlen(copy) > WIFI_OTA_SSID_MAX ||
        strlen(password) > WIFI_OTA_PASSWORD_MAX) {
        ESP_LOGW(TAG, "WiFiDiag phone rejected: invalid SSID/password length");
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "WiFiDiag phone hotspot selected ssid='%s' current_ap='%.*s' ap_info=%s auto_connect=%d",
             copy, 32, (const char *)current_ap.ssid, esp_err_to_name(ap_err), s_auto_connect);
    event_log_add("WiFiDiag phone hotspot selected");
    stop_current_web_connection();
    atomic_store(&s_runtime_source, true);
    strcpy(s_ssid, copy);
    strcpy(s_password, password);
    ESP_LOGI(TAG, "runtime Wi-Fi configuration received over BLE (not saved)");
    return wifi_ota_start_runtime_web();
}

static esp_err_t wifi_ota_get_config_locked(char *out, size_t out_size);
esp_err_t wifi_ota_get_config(char *out, size_t out_size)
{
    if (!s_state_mutex) { return ESP_ERR_INVALID_STATE; }
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    esp_err_t result = wifi_ota_get_config_locked(out, out_size);
    xSemaphoreGiveRecursive(s_state_mutex);
    return result;
}
static esp_err_t wifi_ota_get_config_locked(char *out, size_t out_size)
{
    if (!out || out_size == 0) return ESP_ERR_INVALID_ARG;
    /* Never expose the Wi-Fi password over BLE. */
    int written = snprintf(out, out_size, "%s\n%s", s_saved_ssid, s_saved_url);
    return written < 0 || (size_t)written >= out_size ? ESP_ERR_NO_MEM : ESP_OK;
}

static esp_err_t wifi_ota_get_web_config_locked(char *out, size_t out_size);
esp_err_t wifi_ota_get_web_config(char *out, size_t out_size)
{
    if (!s_state_mutex) { return ESP_ERR_INVALID_STATE; }
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    esp_err_t result = wifi_ota_get_web_config_locked(out, out_size);
    xSemaphoreGiveRecursive(s_state_mutex);
    return result;
}
static esp_err_t wifi_ota_get_web_config_locked(char *out, size_t out_size)
{
    if (!out || out_size == 0) return ESP_ERR_INVALID_ARG;
    /* Do not expose the saved password in the browser. An empty password
     * submitted by the Web UI means keep the current password. */
    int written = snprintf(out, out_size, "%s\n\n%s", s_saved_ssid, s_saved_url);
    return written < 0 || (size_t)written >= out_size ? ESP_ERR_NO_MEM : ESP_OK;
}

static bool wifi_ota_password_configured_locked(void);
bool wifi_ota_password_configured(void)
{
    if (!s_state_mutex) { return false; }
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    bool result = wifi_ota_password_configured_locked();
    xSemaphoreGiveRecursive(s_state_mutex);
    return result;
}
static bool wifi_ota_password_configured_locked(void)
{
    return s_saved_password[0] != '\0';
}

static bool wifi_ota_config_matches_locked(const char *ssid, const char *url);
bool wifi_ota_config_matches(const char *ssid, const char *url)
{
    if (!s_state_mutex) { return false; }
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    bool result = wifi_ota_config_matches_locked(ssid, url);
    xSemaphoreGiveRecursive(s_state_mutex);
    return result;
}
static bool wifi_ota_config_matches_locked(const char *ssid, const char *url)
{
    return ssid && url && strcmp(s_saved_ssid, ssid) == 0 && strcmp(s_saved_url, url) == 0;
}

static esp_err_t wifi_ota_get_status_locked(char *out, size_t out_size);
esp_err_t wifi_ota_get_status(char *out, size_t out_size)
{
    if (!s_state_mutex) { return ESP_ERR_INVALID_STATE; }
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    esp_err_t result = wifi_ota_get_status_locked(out, out_size);
    xSemaphoreGiveRecursive(s_state_mutex);
    return result;
}
static esp_err_t wifi_ota_get_status_locked(char *out, size_t out_size)
{
    if (!out || out_size == 0) return ESP_ERR_INVALID_ARG;
    int written = snprintf(out, out_size, "%s", s_status[0] ? s_status : "IDLE");
    return written < 0 || (size_t)written >= out_size ? ESP_ERR_NO_MEM : ESP_OK;
}

static bool wifi_ota_get_auto_connect_locked(void);
bool wifi_ota_get_auto_connect(void)
{
    if (!s_state_mutex) { return false; }
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    bool result = wifi_ota_get_auto_connect_locked();
    xSemaphoreGiveRecursive(s_state_mutex);
    return result;
}
static bool wifi_ota_get_auto_connect_locked(void)
{
    return s_auto_connect;
}

static esp_err_t wifi_ota_set_auto_connect_locked(bool enabled);
esp_err_t wifi_ota_set_auto_connect(bool enabled)
{
    if (!s_state_mutex) { return ESP_ERR_INVALID_STATE; }
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    esp_err_t result = wifi_ota_set_auto_connect_locked(enabled);
    xSemaphoreGiveRecursive(s_state_mutex);
    return result;
}
static esp_err_t wifi_ota_set_auto_connect_locked(bool enabled)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_OTA_NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, "auto_connect", enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) {
        s_auto_connect = enabled;
        ESP_LOGI(TAG, "WiFiDiag auto_connect changed=%d saved_ssid='%s'", enabled, s_saved_ssid);
    }
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
    if (err != ESP_OK) goto failed_netif;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) goto failed_wifi;
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                     wifi_event_handler, NULL);
    if (err != ESP_OK) goto failed_wifi;
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                     wifi_event_handler, NULL);
    if (err != ESP_OK) {
        esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler);
        goto failed_wifi;
    }
    s_wifi_stack_ready = true;
    return ESP_OK;
failed_wifi:
    esp_wifi_deinit();
failed_netif:
    esp_netif_destroy_default_wifi(s_sta_netif);
    s_sta_netif = NULL;
    return err;
}

static esp_err_t connect_wifi(void)
{
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFiDiag connect source=%s target='%s' connected=%d status=%s wait_ip_ms=30000",
             wifi_source(), s_ssid, s_wifi_connected, s_status);
    esp_err_t err = ensure_wifi_stack();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFiDiag ensure_wifi_stack failed: %s", esp_err_to_name(err));
        { esp_err_t result = err; xSemaphoreGiveRecursive(s_state_mutex); return result; }
    }
    if (s_wifi_connected) {
        wifi_ap_record_t ap = {0};
        esp_err_t ap_err = esp_wifi_sta_get_ap_info(&ap);
        ESP_LOGI(TAG, "WiFiDiag reuse existing connection actual_ssid='%.*s' ap_info=%s (no new attempt)",
                 32, (const char *)ap.ssid, esp_err_to_name(ap_err));
        { esp_err_t result = ESP_OK; xSemaphoreGiveRecursive(s_state_mutex); return result; }
    }
    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT);
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, s_ssid, strnlen(s_ssid, sizeof(config.sta.ssid)));
    memcpy(config.sta.password, s_password, strnlen(s_password, sizeof(config.sta.password)));
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFiDiag set_mode failed: %s", esp_err_to_name(err));
        { esp_err_t result = err; xSemaphoreGiveRecursive(s_state_mutex); return result; }
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFiDiag set_config failed: %s", esp_err_to_name(err));
        { esp_err_t result = err; xSemaphoreGiveRecursive(s_state_mutex); return result; }
    }
    s_reconnect_enabled = true;
    err = esp_wifi_start();
    ESP_LOGI(TAG, "WiFiDiag esp_wifi_start=%s; waiting for GOT_IP", esp_err_to_name(err));
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { esp_err_t result = err; xSemaphoreGiveRecursive(s_state_mutex); return result; }
    /* Wait for the IP event only.  WIFI_EVENT_STA_DISCONNECTED may be a
     * delayed notification from the connection that was just stopped. */
    xSemaphoreGiveRecursive(s_state_mutex);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                           WIFI_CONNECTED_BIT,
                                           pdTRUE, pdFALSE, pdMS_TO_TICKS(30000));
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    if (!(bits & WIFI_CONNECTED_BIT)) {
        wifi_ap_record_t ap = {0};
        esp_err_t ap_err = esp_wifi_sta_get_ap_info(&ap);
        unsigned reason = atomic_load(&s_last_disconnect_reason);
        ESP_LOGW(TAG, "WiFiDiag IP timeout source=%s target='%s' associated=%d actual_ssid='%.*s' last_reason=%u(%s) attempts=%u reconnect=%d",
                 wifi_source(), s_ssid, ap_err == ESP_OK, 32, (const char *)ap.ssid,
                 reason, wifi_reason_name(reason), atomic_load(&s_connect_attempts), s_reconnect_enabled);
        event_log_add("WiFiDiag IP timeout: reason=%u", reason);
    }
    { esp_err_t result = (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT; xSemaphoreGiveRecursive(s_state_mutex); return result; }
}

static void stop_current_web_connection(void)
{
    ESP_LOGI(TAG, "WiFiDiag deliberate stop source=%s target='%s' connected=%d web_task=%d",
             wifi_source(), s_ssid, s_wifi_connected, s_web_task_running);
    s_reconnect_enabled = false;
    /* One HTTP server survives a station reconnect; no HTTP task join here. */
    if (s_wifi_stack_ready) {
        esp_wifi_disconnect();
        esp_wifi_stop();
    }
    s_wifi_connected = false;
}

static bool wifi_ota_get_auto_update_locked(void);
bool wifi_ota_get_auto_update(void)
{
    if (!s_state_mutex) { return false; }
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    bool result = wifi_ota_get_auto_update_locked();
    xSemaphoreGiveRecursive(s_state_mutex);
    return result;
}
static bool wifi_ota_get_auto_update_locked(void)
{
    return false;
}

static esp_err_t wifi_ota_set_auto_update_locked(bool enabled);
esp_err_t wifi_ota_set_auto_update(bool enabled)
{
    if (enabled) return ESP_ERR_NOT_SUPPORTED;
    if (!s_state_mutex) { return ESP_ERR_INVALID_STATE; }
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    esp_err_t result = wifi_ota_set_auto_update_locked(enabled);
    xSemaphoreGiveRecursive(s_state_mutex);
    return result;
}
static esp_err_t wifi_ota_set_auto_update_locked(bool enabled)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_OTA_NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, "auto_update", enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static void wifi_ota_prepare_restart_locked(void);
void wifi_ota_prepare_restart(void)
{
    if (!s_state_mutex) { return; }
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    wifi_ota_prepare_restart_locked();
    xSemaphoreGiveRecursive(s_state_mutex);

}
static void wifi_ota_prepare_restart_locked(void)
{
    ESP_LOGI(TAG, "WiFiDiag deliberate stop for restart source=%s target='%s'", wifi_source(), s_ssid);
    s_reconnect_enabled = false;
    if (s_wifi_stack_ready) {
        esp_err_t err = esp_wifi_disconnect();
        if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT) {
            ESP_LOGW(TAG, "Wi-Fi disconnect before restart: %s", esp_err_to_name(err));
        }
        err = esp_wifi_stop();
        if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
            ESP_LOGW(TAG, "Wi-Fi stop before restart: %s", esp_err_to_name(err));
        }
    }
    s_wifi_connected = false;
    ESP_LOGI(TAG, "Wi-Fi prepared for restart");
}

static void web_task(void *arg)
{
    (void)arg;
    set_status("CONNECTING_WEB");
    esp_err_t err = connect_wifi();
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    if (err == ESP_OK) {
        err = web_ui_start();
        if (err == ESP_OK) {
            s_web_active = true;
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(s_sta_netif, &ip_info) == ESP_OK) {
                if (atomic_load(&s_runtime_source)) set_web_ready_status(&ip_info);
                else set_status("WIFI_CONNECTED");
            } else {
                if (atomic_load(&s_runtime_source)) set_web_ready_status(NULL);
                else set_status("WIFI_CONNECTED");
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
    xSemaphoreGiveRecursive(s_state_mutex);
    vTaskDelete(NULL);
}

static esp_err_t start_web_task(bool respect_auto_connect)
{
    if (!s_initialized || (respect_auto_connect && !home_auto_connect()) ||
        s_ssid[0] == '\0' || s_password[0] == '\0') {
        ESP_LOGW(TAG, "WiFiDiag Web start rejected initialized=%d respect_auto=%d auto_connect=%d ssid_present=%d password_present=%d",
                 s_initialized, respect_auto_connect, s_auto_connect, s_ssid[0] != '\0', s_password[0] != '\0');
        return ESP_ERR_INVALID_STATE;
    }
    if ((s_web_active && s_wifi_connected) || s_web_task_running) {
        ESP_LOGI(TAG, "WiFiDiag Web start skipped: web_active=%d web_task=%d source=%s target='%s'",
                 s_web_active, s_web_task_running, wifi_source(), s_ssid);
        return ESP_OK;
    }
    s_web_task_running = true;
    if (xTaskCreate(web_task, "wifi_web", 6144, NULL, 4, NULL) != pdPASS) {
        s_web_task_running = false;
        ESP_LOGE(TAG, "WiFiDiag Web task creation failed: no memory");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "WiFiDiag Web task scheduled source=%s target='%s'", wifi_source(), s_ssid);
    return ESP_OK;
}

static esp_err_t wifi_ota_start_web_locked(void);
esp_err_t wifi_ota_start_web(void)
{
    if (!s_state_mutex) { return ESP_ERR_INVALID_STATE; }
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    esp_err_t result = wifi_ota_start_web_locked();
    xSemaphoreGiveRecursive(s_state_mutex);
    return result;
}
static esp_err_t wifi_ota_start_web_locked(void)
{
    if (s_task_running || s_web_task_running) return ESP_ERR_INVALID_STATE;
    activate_saved_config();
    return start_web_task(true);
}

void wifi_ota_set_mqtt_required(bool enabled)
{
    bool changed = atomic_exchange(&s_mqtt_required, enabled) != enabled;
    if (!enabled || !s_state_mutex ||
        xSemaphoreTakeRecursive(s_state_mutex, 0) != pdTRUE) return;
    int64_t now = esp_timer_get_time();
    if (changed) s_mqtt_wifi_retry_us = 0;
    if (s_initialized && !atomic_load(&s_runtime_source) && !s_task_running &&
        !s_web_task_running && !s_wifi_connected && now >= s_mqtt_wifi_retry_us) {
        s_mqtt_wifi_retry_us = now + 30000000;
        if (!s_reconnect_enabled) {
            esp_err_t err = wifi_ota_start_web_locked();
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
                ESP_LOGW(TAG, "MQTT home Wi-Fi start failed: %s", esp_err_to_name(err));
        }
    }
    xSemaphoreGiveRecursive(s_state_mutex);
}

esp_err_t wifi_ota_request_home_wifi(void)
{
    if (!s_state_mutex || xSemaphoreTakeRecursive(s_state_mutex, 0) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    esp_err_t err = ESP_OK;
    if (home_auto_connect() && !atomic_load(&s_runtime_source) && !s_task_running &&
        !s_web_task_running && !s_wifi_connected)
        err = wifi_ota_start_web_locked();
    xSemaphoreGiveRecursive(s_state_mutex);
    return err;
}

static esp_err_t wifi_ota_start_runtime_web_locked(void);
esp_err_t wifi_ota_start_runtime_web(void)
{
    if (!s_state_mutex) { return ESP_ERR_INVALID_STATE; }
    xSemaphoreTakeRecursive(s_state_mutex, portMAX_DELAY);
    esp_err_t result = wifi_ota_start_runtime_web_locked();
    xSemaphoreGiveRecursive(s_state_mutex);
    return result;
}
static esp_err_t wifi_ota_start_runtime_web_locked(void)
{
    return start_web_task(false);
}

/* Firmware is downloaded and cached by Android, then uploaded to /api/ota. */
esp_err_t wifi_ota_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t wifi_ota_check_for_update(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}
