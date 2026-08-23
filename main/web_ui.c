#include "web_ui.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_config.h"
#include "ble_phone.h"
#include "event_log.h"
#include "led.h"
#include "lock_control.h"
#include "ulp_controller.h"
#include "wifi_ota.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "web_ui";
static httpd_handle_t s_server;
static bool s_restart_pending;

extern const uint8_t web_index_html_start[] asm("_binary_index_html_start");
extern const uint8_t web_index_html_end[] asm("_binary_index_html_end");

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    // EMBED_TXTFILES appends one NUL byte; it is not part of the HTTP body.
    size_t html_length = (size_t)(web_index_html_end - web_index_html_start - 1);
    return httpd_resp_send(req, (const char *)web_index_html_start, html_length);
}

static esp_err_t status_get(httpd_req_t *req)
{
    ble_phone_status_t status;
    ble_phone_get_status(&status);
    char json[384];
    snprintf(json, sizeof(json),
             "{\"version\":\"%s\",\"build\":\"%s\",\"open\":%s,\"connected\":%s,"
             "\"encrypted\":%s,\"bonded\":%s,\"rssi\":",
             APP_VERSION, APP_BUILD_TIMESTAMP,
             lock_control_is_open() ? "true" : "false",
             status.connected ? "true" : "false",
             status.encrypted ? "true" : "false",
             status.bonded ? "true" : "false");
    size_t pos = strlen(json);
    if (status.rssi_valid) pos += snprintf(json + pos, sizeof(json) - pos, "%d", status.rssi);
    else pos += snprintf(json + pos, sizeof(json) - pos, "null");
    snprintf(json + pos, sizeof(json) - pos, ",\"uptime_s\":%lld}",
             (long long)(esp_timer_get_time() / 1000000));
    return send_json(req, json);
}

static const char *ulp_lock_state_name(lock_state_t state)
{
    switch (state) {
        case LOCK_STATE_STARTING: return "Запуск";
        case LOCK_STATE_UNLOCKED: return "Открыт";
        case LOCK_STATE_LOCKED: return "Закрыт";
        case LOCK_STATE_FAULT: return "Неисправность";
        default: return "Неизвестно";
    }
}

static const char *ulp_fsm_state_name(lock_fsm_state_t state)
{
    switch (state) {
        case LOCK_FSM_UNLOCKED_SCAN: return "Открыт: измерение";
        case LOCK_FSM_CLOSE_WAIT_PLATE: return "Закрытие: ожидание пластины";
        case LOCK_FSM_CLOSE_ENERGIZE: return "Закрытие: включение соленоида";
        case LOCK_FSM_CLOSE_VERIFY: return "Закрытие: проверка";
        case LOCK_FSM_LOCKED: return "Закрыт";
        case LOCK_FSM_OPEN_DISCHARGE: return "Открытие: разряд";
        case LOCK_FSM_OPEN_VERIFY_CURRENT_OFF: return "Открытие: проверка тока";
        case LOCK_FSM_FAULT: return "Неисправность";
        default: return "Неизвестно";
    }
}

static const char *ulp_plate_state_name(plate_state_t state)
{
    switch (state) {
        case PLATE_CONTACT: return "Контакт";
        case PLATE_GAP_1: return "Зазор 1";
        case PLATE_GAP_2: return "Зазор 2";
        case PLATE_GAP_3: return "Зазор 3";
        case PLATE_GAP_4: return "Зазор 4";
        case PLATE_GAP_5: return "Зазор 5";
        case PLATE_FAR: return "Далеко";
        default: return "Неизвестно";
    }
}

static esp_err_t ulp_get(httpd_req_t *req)
{
    ulp_diagnostics_t d;
    if (!ulp_controller_read_diagnostics(&d)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "ULP snapshot unavailable");
    }
    char json[1500];
    snprintf(json, sizeof(json),
             "{\"lock_state\":%u,\"lock_state_name\":\"%s\","
             "\"fsm_state\":%u,\"fsm_state_name\":\"%s\","
             "\"plate_state\":%u,\"plate_state_name\":\"%s\","
             "\"fault_flags\":%lu,\"event_flags\":%lu,"
             "\"baseline_raw\":%ld,\"vbat_raw\":%ld,"
             "\"sense_raw\":%ld,\"delta_raw\":%ld,"
             "\"normalized_feature\":%lu,\"coil_current_raw\":%lu,"
             "\"measurement_counter\":%lu,\"pulse_actual_us\":%lu,"
             "\"close_verify_state\":%lu,\"close_verify_feature\":%lu,"
             "\"close_verify_delta_raw\":%ld,"
             "\"close_attempt_counter\":%lu,"
             "\"command_ack_sequence\":%lu,\"event_sequence\":%lu}",
             (unsigned)d.lock_state, ulp_lock_state_name(d.lock_state),
             (unsigned)d.fsm_state, ulp_fsm_state_name(d.fsm_state),
             (unsigned)d.plate_state, ulp_plate_state_name(d.plate_state),
             (unsigned long)d.fault_flags, (unsigned long)d.event_flags,
             (long)d.baseline_raw, (long)d.vbat_raw, (long)d.sense_raw,
             (long)d.delta_raw, (unsigned long)d.normalized_feature,
             (unsigned long)d.coil_current_raw, (unsigned long)d.measurement_counter,
             (unsigned long)d.pulse_actual_us, (unsigned long)d.close_verify_state,
             (unsigned long)d.close_verify_feature, (long)d.close_verify_delta_raw,
             (unsigned long)d.close_attempt_counter,
             (unsigned long)d.command_ack_sequence, (unsigned long)d.event_sequence);
    return send_json(req, json);
}

static esp_err_t config_get(httpd_req_t *req)
{
    app_config_t cfg;
    app_config_get(&cfg);
    char json[420];
    snprintf(json, sizeof(json),
             "{\"phone_rssi_threshold\":%d,\"phone_rssi_hold_threshold\":%d,"
             "\"ble_connect_rssi_threshold\":%d,\"ble_disconnect_rssi_threshold\":%d,"
             "\"ble_tx_power_dbm\":%d,"
             "\"long_open_ms\":%lu,"
             "\"timer_a_ms\":%lu,\"timer_b_s\":%lu,\"lock_gpio\":%d,\"lock_open_level\":%s}",
             cfg.phone_rssi_threshold, cfg.phone_rssi_hold_threshold,
             cfg.ble_connect_rssi_threshold, cfg.ble_disconnect_rssi_threshold,
             cfg.ble_tx_power_dbm,
             (unsigned long)cfg.long_open_ms,
             (unsigned long)cfg.timer_a_ms, (unsigned long)cfg.timer_b_s, cfg.lock_gpio,
             cfg.lock_open_level ? "true" : "false");
    return send_json(req, json);
}

static esp_err_t config_post(httpd_req_t *req)
{
    char query[500], rssi[16], rssi_hold[16], ble_connect_rssi[16], ble_disconnect_rssi[16],
         long_open[16], timer_a[16], timer_b[16],
         lock_gpio[16], open_level[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "phone_rssi_threshold", rssi, sizeof(rssi)) != ESP_OK ||
        httpd_query_key_value(query, "phone_rssi_hold_threshold", rssi_hold, sizeof(rssi_hold)) != ESP_OK ||
        httpd_query_key_value(query, "ble_connect_rssi_threshold", ble_connect_rssi, sizeof(ble_connect_rssi)) != ESP_OK ||
        httpd_query_key_value(query, "ble_disconnect_rssi_threshold", ble_disconnect_rssi, sizeof(ble_disconnect_rssi)) != ESP_OK ||
        httpd_query_key_value(query, "long_open_ms", long_open, sizeof(long_open)) != ESP_OK ||
        httpd_query_key_value(query, "timer_a_ms", timer_a, sizeof(timer_a)) != ESP_OK ||
        httpd_query_key_value(query, "timer_b_s", timer_b, sizeof(timer_b)) != ESP_OK ||
        httpd_query_key_value(query, "lock_gpio", lock_gpio, sizeof(lock_gpio)) != ESP_OK ||
        httpd_query_key_value(query, "lock_open_level", open_level, sizeof(open_level)) != ESP_OK) {
        ESP_LOGE(TAG, "Настройки замка: HTTP 400, отсутствуют параметры; query='%s'", query);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing parameters");
    }
    ESP_LOGI(TAG, "Настройки замка: запрос connect=%s disconnect=%s phone=%s hold=%s long=%s A=%s B=%s gpio=%s level=%s",
             ble_connect_rssi, ble_disconnect_rssi, rssi, rssi_hold, long_open,
             timer_a, timer_b, lock_gpio, open_level);
    esp_err_t err = app_config_set_logic(atoi(rssi), atoi(rssi_hold),
                                         atoi(ble_connect_rssi), atoi(ble_disconnect_rssi),
                                         strtoul(long_open, NULL, 10),
                                         strtoul(timer_a, NULL, 10),
                                         strtoul(timer_b, NULL, 10), atoi(lock_gpio),
                                         atoi(open_level) != 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Настройки замка: HTTP 400, app_config_set_logic отказал: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "Настройки замка: NVS запись успешна, HTTP 200");
    /* The lock task reads app_config_get() on each control cycle.  Do not
     * re-issue the current ULP command here: during an active BLE/Web UI
     * session that auxiliary operation may fail after NVS was already
     * written, producing a false save error. */
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t wifi_get(httpd_req_t *req)
{
    char value[WIFI_OTA_SSID_MAX + WIFI_OTA_PASSWORD_MAX + WIFI_OTA_URL_MAX + 4];
    if (wifi_ota_get_web_config(value, sizeof(value)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wi-Fi config unavailable");
    }
    char *separator = strchr(value, '\n');
    if (!separator) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wi-Fi config invalid");
    *separator = '\0';
    char *url = separator + 1;
    separator = strchr(url, '\n');
    if (!separator) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wi-Fi config invalid");
    *separator = '\0';
    char json[WIFI_OTA_SSID_MAX + WIFI_OTA_URL_MAX + 180];
    snprintf(json, sizeof(json),
             "{\"ssid\":\"%s\",\"url\":\"%s\",\"password_configured\":%s,\"auto_connect\":%s}",
             value, separator + 1, wifi_ota_password_configured() ? "true" : "false",
             wifi_ota_get_auto_connect() ? "true" : "false");
    return send_json(req, json);
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool form_url_decode(char *value)
{
    char *read = value;
    char *write = value;
    while (*read) {
        if (*read == '+') {
            *write++ = ' ';
            read++;
        } else if (*read == '%') {
            int high = hex_value(read[1]);
            int low = read[1] ? hex_value(read[2]) : -1;
            if (high < 0 || low < 0) return false;
            *write++ = (char)((high << 4) | low);
            read += 3;
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
    return true;
}

static esp_err_t wifi_form_read(httpd_req_t *req, char *form, size_t form_size)
{
    if (httpd_req_get_url_query_str(req, form, form_size) == ESP_OK) return ESP_OK;
    if (req->content_len <= 0 || (size_t)req->content_len >= form_size) return ESP_ERR_INVALID_SIZE;
    size_t received = 0;
    while (received < (size_t)req->content_len) {
        int count = httpd_req_recv(req, form + received, req->content_len - received);
        if (count <= 0) return ESP_FAIL;
        received += (size_t)count;
    }
    form[received] = '\0';
    return ESP_OK;
}

static esp_err_t wifi_post(httpd_req_t *req)
{
    char query[1024], ssid[WIFI_OTA_SSID_MAX * 3 + 1],
         password[WIFI_OTA_PASSWORD_MAX * 3 + 1],
         encoded_url[WIFI_OTA_URL_MAX * 3 + 1],
         url[WIFI_OTA_URL_MAX + 1];
    if (wifi_form_read(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "ssid", ssid, sizeof(ssid)) != ESP_OK ||
        httpd_query_key_value(query, "url", encoded_url, sizeof(encoded_url)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing Wi-Fi parameters");
    }
    password[0] = '\0';
    httpd_query_key_value(query, "password", password, sizeof(password));
    if (!form_url_decode(ssid) || !form_url_decode(password) ||
        !form_url_decode(encoded_url) || strlen(ssid) > WIFI_OTA_SSID_MAX ||
        strlen(password) > WIFI_OTA_PASSWORD_MAX || strlen(encoded_url) > WIFI_OTA_URL_MAX) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid Wi-Fi parameters");
    }
    strcpy(url, encoded_url);
    if (password[0] == '\0' && wifi_ota_config_matches(ssid, url)) {
        ESP_LOGI(TAG, "Web UI Wi-Fi/OTA save skipped: configuration unchanged");
        return send_json(req, "{\"ok\":true,\"unchanged\":true}");
    }
    char payload[WIFI_OTA_SSID_MAX + WIFI_OTA_PASSWORD_MAX + WIFI_OTA_URL_MAX + 4];
    int length = snprintf(payload, sizeof(payload), "%s\n%s\n%s", ssid, password, url);
    if (length < 0 || (size_t)length >= sizeof(payload)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Wi-Fi/OTA configuration is too long");
    }
    esp_err_t config_err = wifi_ota_set_config(payload, (size_t)length);
    if (config_err != ESP_OK) {
        ESP_LOGE(TAG, "Web UI Wi-Fi/OTA save failed: %s", esp_err_to_name(config_err));
        char error[96];
        snprintf(error, sizeof(error), "Wi-Fi/OTA save failed: %s", esp_err_to_name(config_err));
        return httpd_resp_send_err(req, config_err == ESP_ERR_INVALID_ARG ? HTTPD_400_BAD_REQUEST : HTTPD_500_INTERNAL_SERVER_ERROR, error);
    }
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t ble_power_post(httpd_req_t *req)
{
    char query[80], power[16];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "power_dbm", power, sizeof(power)) != ESP_OK) {
        ESP_LOGE(TAG, "BLE power: HTTP 400, отсутствует power_dbm; query='%s'", query);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing power_dbm");
    }
    ESP_LOGI(TAG, "BLE power: запрос power_dbm=%s", power);
    esp_err_t err = app_config_set_ble_tx_power(atoi(power));
    if (err == ESP_OK) err = ble_phone_apply_tx_power();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE power: HTTP 400, ошибка сохранения/применения: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "BLE power: NVS запись успешна, HTTP 200");
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t wifi_auto_post(httpd_req_t *req)
{
    char query[64], auto_connect[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "auto_connect", auto_connect, sizeof(auto_connect)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing auto-connect parameter");
    }
    esp_err_t err = wifi_ota_set_auto_connect(strcmp(auto_connect, "1") == 0 ||
                                               strcmp(auto_connect, "true") == 0);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    return send_json(req, "{\"ok\":true}");
}

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

static esp_err_t restart_post(httpd_req_t *req)
{
    if (!s_restart_pending) {
        s_restart_pending = true;
        if (xTaskCreate(restart_task, "web_restart", 2048, NULL, 5, NULL) != pdPASS) {
            s_restart_pending = false;
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                       "restart task unavailable");
        }
        event_log_add("Web: команда перезагрузки принята");
    }
    return send_json(req, "{\"ok\":true,\"accepted\":true}");
}

static esp_err_t registration_get(httpd_req_t *req)
{
    bool allowed;
    uint32_t remaining;
    ble_phone_get_registration(&allowed, &remaining);
    char json[80];
    snprintf(json, sizeof(json), "{\"allowed\":%s,\"remaining_s\":%lu}",
             allowed ? "true" : "false", (unsigned long)remaining);
    return send_json(req, json);
}

static esp_err_t registration_post(httpd_req_t *req)
{
    ble_phone_allow_registration();
    return send_json(req, "{\"ok\":true,\"remaining_s\":60}");
}

static esp_err_t bonds_get(httpd_req_t *req)
{
    ble_phone_bond_t bonds[BLE_PHONE_MAX_BONDS];
    size_t count = ble_phone_get_bonds(bonds, BLE_PHONE_MAX_BONDS);
    char json[2048];
    size_t pos = snprintf(json, sizeof(json), "[");
    for (size_t i = 0; i < count; i++) {
        pos += snprintf(json + pos, sizeof(json) - pos,
                        "%s{\"address\":\"%s\",\"type\":%u}",
                        i ? "," : "", bonds[i].address, bonds[i].address_type);
    }
    snprintf(json + pos, sizeof(json) - pos, "]");
    return send_json(req, json);
}

static esp_err_t bonds_delete(httpd_req_t *req)
{
    char query[128], address[24];
    esp_err_t err;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "address", address, sizeof(address)) == ESP_OK) {
        err = ble_phone_delete_bond(address);
    } else {
        err = ble_phone_delete_all_bonds();
    }
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, err == ESP_ERR_NOT_FOUND ? HTTPD_404_NOT_FOUND : HTTPD_500_INTERNAL_SERVER_ERROR,
                                   esp_err_to_name(err));
    }
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t lock_test_post(httpd_req_t *req)
{
    esp_err_t err = lock_request_test_open();
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    event_log_add("Web: тестовый запрос открытия");
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t ota_post(httpd_req_t *req)
{
    if (req->content_len <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty firmware");
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    esp_ota_handle_t handle;
    esp_err_t err = esp_ota_begin(partition, req->content_len, &handle);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    char buffer[2048];
    int remaining = req->content_len;
    while (remaining > 0) {
        int received = httpd_req_recv(req, buffer, remaining > (int)sizeof(buffer) ? sizeof(buffer) : remaining);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (received <= 0) { esp_ota_abort(handle); return ESP_FAIL; }
        err = esp_ota_write(handle, buffer, received);
        if (err != ESP_OK) { esp_ota_abort(handle); return err; }
        remaining -= received;
    }
    if ((err = esp_ota_end(handle)) == ESP_OK) err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    led_set_state(LED_STATE_OTA);
    httpd_resp_sendstr(req, "Прошивка записана");
    vTaskDelay(pdMS_TO_TICKS(700));
    esp_restart();
    return ESP_OK;
}

esp_err_t web_ui_start(void)
{
    if (s_server) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    /* The Web UI exposes 21 routes (including BLE, ULP, Wi-Fi and OTA
     * controls). Leave a small margin for future routes. */
    config.max_uri_handlers = 24;
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) return err;
    const httpd_uri_t uris[] = {
        {.uri="/", .method=HTTP_GET, .handler=index_get},
        {.uri="/api/status", .method=HTTP_GET, .handler=status_get},
        {.uri="/api/ulp", .method=HTTP_GET, .handler=ulp_get},
        {.uri="/api/config", .method=HTTP_GET, .handler=config_get},
        {.uri="/api/config", .method=HTTP_POST, .handler=config_post},
        {.uri="/api/ble/power", .method=HTTP_POST, .handler=ble_power_post},
        {.uri="/api/wifi", .method=HTTP_GET, .handler=wifi_get},
        {.uri="/api/wifi", .method=HTTP_POST, .handler=wifi_post},
        {.uri="/api/wifi/auto", .method=HTTP_POST, .handler=wifi_auto_post},
        {.uri="/api/system/restart", .method=HTTP_POST, .handler=restart_post},
        {.uri="/api/ble/registration", .method=HTTP_GET, .handler=registration_get},
        {.uri="/api/ble/registration", .method=HTTP_POST, .handler=registration_post},
        {.uri="/api/ble/bonds", .method=HTTP_GET, .handler=bonds_get},
        {.uri="/api/ble/bonds", .method=HTTP_DELETE, .handler=bonds_delete},
        {.uri="/api/lock/test", .method=HTTP_POST, .handler=lock_test_post},
        {.uri="/api/ota", .method=HTTP_POST, .handler=ota_post},
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        err = httpd_register_uri_handler(s_server, &uris[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "register failed: %s", uris[i].uri);
            httpd_stop(s_server);
            s_server = NULL;
            return err;
        }
    }
    event_log_add("Web UI started");
    return ESP_OK;
}

esp_err_t web_ui_stop(void)
{
    if (!s_server) return ESP_OK;
    esp_err_t err = httpd_stop(s_server);
    if (err == ESP_OK) {
        s_server = NULL;
        event_log_add("Web UI stopped");
    }
    return err;
}
