#include "web_ui.h"
#include "lock_ai_adapter.h"
#include "ai_agent_http.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "app_config.h"
#include "ble_phone.h"
#include "event_log.h"
#include "lock_control.h"
#include "thingspeak_config.h"
#include "ulp_controller.h"
#include "wifi_ota.h"
#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "web_ui";
static httpd_handle_t s_server;
static bool json_append_string(char *json, size_t size, size_t *pos, const char *value);
extern const uint8_t web_index_html_start[] asm("_binary_index_html_start");
extern const uint8_t web_index_html_end[] asm("_binary_index_html_end");
extern const uint8_t web_calibration_html_start[] asm("_binary_calibration_html_start");
extern const uint8_t web_calibration_html_end[] asm("_binary_calibration_html_end");

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
    char json[640];
    esp_err_t err = lock_ai_status_json(json, sizeof(json));
    return err == ESP_OK ? send_json(req, json) :
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
}

static esp_err_t ulp_get(httpd_req_t *req)
{
    char *json = malloc(4096);
    if (!json) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
    esp_err_t err = lock_ai_ulp_json(json, 4096);
    esp_err_t sent = err == ESP_OK ? send_json(req, json) :
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ULP snapshot unavailable");
    free(json);
    return sent;
}

static esp_err_t calibration_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    size_t length = (size_t)(web_calibration_html_end - web_calibration_html_start - 1);
    return httpd_resp_send(req, (const char *)web_calibration_html_start, length);
}

static esp_err_t ulp_calibration_post(httpd_req_t *req)
{
    char query[256] = {0}, action[16] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "action", action, sizeof(action)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing action");
    }
    if (strcmp(action, "start") == 0) {
        esp_err_t err = ulp_controller_calibration_start();
        return err == ESP_OK ? send_json(req, "{\"ok\":true}") :
               httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    }
    if (strcmp(action, "cancel") == 0) {
        char source[16] = "unknown";
        (void)httpd_query_key_value(query, "source", source, sizeof(source));
        event_log_add("CAL cancel source=%s", source);
        ulp_controller_calibration_cancel();
        return send_json(req, "{\"ok\":true}");
    }
    if (strcmp(action, "trace") == 0) {
        char step[12] = {0}, event[20] = {0}, data[48] = {0};
        if (httpd_query_key_value(query, "step", step, sizeof(step)) != ESP_OK ||
            httpd_query_key_value(query, "event", event, sizeof(event)) != ESP_OK) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing trace fields");
        }
        (void)httpd_query_key_value(query, "data", data, sizeof(data));
        if (strspn(step, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != strlen(step) ||
            strspn(event, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != strlen(event) ||
            strspn(data, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-=,. ") != strlen(data)) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid trace characters");
        }
        event_log_add("CAL step=%s event=%s %s", step, event, data);
        return send_json(req, "{\"ok\":true}");
    }
    if (strcmp(action, "verify") == 0) {
        esp_err_t err = ulp_controller_calibration_verify();
        return err == ESP_OK ? send_json(req, "{\"ok\":true}") :
               httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    }
    if (strcmp(action, "save") == 0) {
        char closed[16], open[16], verify[16];
        if (httpd_query_key_value(query, "closed", closed, sizeof(closed)) != ESP_OK ||
            httpd_query_key_value(query, "open", open, sizeof(open)) != ESP_OK ||
            httpd_query_key_value(query, "verify", verify, sizeof(verify)) != ESP_OK) {
            event_log_add("CAL save rejected: missing points");
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing calibration points");
        }
        const char *points[] = {closed, open, verify};
        int32_t raw[3];
        for (int i = 0; i < 3; i++) {
            char *end;
            errno = 0;
            long value = strtol(points[i], &end, 10);
            if (errno || end == points[i] || *end || value < 0 || value > 4095) {
                event_log_add("CAL save rejected: invalid point index=%d", i);
                return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ADC points must be 0..4095");
            }
            raw[i] = (int32_t)value;
        }
        esp_err_t err = ulp_controller_calibration_save(raw[0], raw[1], raw[2]);
        return err == ESP_OK ? send_json(req, "{\"ok\":true}") :
               httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, esp_err_to_name(err));
    }
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown action");
}

static esp_err_t config_get(httpd_req_t *req)
{
    app_config_t cfg;
    app_config_get(&cfg);
    char json[320];
    snprintf(json, sizeof(json),
             "{\"phone_rssi_threshold\":%d,"
             "\"shunt_gpio\":%u,"
             "\"serial_log_enabled\":%s,"
             "\"timer_a_ms\":%lu}",
             cfg.phone_rssi_threshold,
             cfg.shunt_gpio,
             cfg.serial_log_enabled ? "true" : "false",
             (unsigned long)cfg.timer_a_ms);
    return send_json(req, json);
}

static esp_err_t config_post(httpd_req_t *req)
{
    char query[500], rssi[16], timer_a[16];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "phone_rssi_threshold", rssi, sizeof(rssi)) != ESP_OK ||
        httpd_query_key_value(query, "timer_a_ms", timer_a, sizeof(timer_a)) != ESP_OK) {
        ESP_LOGE(TAG, "Настройки замка: HTTP 400, отсутствуют параметры; query='%s'", query);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing parameters");
    }
    ESP_LOGI(TAG, "Настройки замка: запрос zone=%s A=%s", rssi, timer_a);
    esp_err_t err = app_config_set_logic(atoi(rssi),
                                         strtoul(timer_a, NULL, 10));
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
    char json[WIFI_OTA_SSID_MAX + WIFI_OTA_PASSWORD_MAX + WIFI_OTA_URL_MAX + 180];
    strcpy(json, "{\"ssid\":");
    size_t pos = strlen(json);
    if (!json_append_string(json, sizeof(json), &pos, value))
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SSID too large");
    snprintf(json + pos, sizeof(json) - pos,
             ",\"password_configured\":%s,\"auto_connect\":%s,\"auto_update\":%s}",
             wifi_ota_password_configured() ? "true" : "false",
             wifi_ota_get_auto_connect() ? "true" : "false",
             wifi_ota_get_auto_update() ? "true" : "false");
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
    char query[512], ssid[WIFI_OTA_SSID_MAX * 3 + 1],
         password[WIFI_OTA_PASSWORD_MAX * 3 + 1];
    if (wifi_form_read(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "ssid", ssid, sizeof(ssid)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing Wi-Fi parameters");
    }
    password[0] = '\0';
    httpd_query_key_value(query, "password", password, sizeof(password));
    if (!form_url_decode(ssid) || !form_url_decode(password) ||
        strlen(ssid) > WIFI_OTA_SSID_MAX || strlen(password) > WIFI_OTA_PASSWORD_MAX) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid Wi-Fi parameters");
    }
    if (password[0] == '\0' && wifi_ota_config_matches(ssid, "")) {
        ESP_LOGI(TAG, "Web UI Wi-Fi/OTA save skipped: configuration unchanged");
        return send_json(req, "{\"ok\":true,\"unchanged\":true}");
    }
    char payload[WIFI_OTA_SSID_MAX + WIFI_OTA_PASSWORD_MAX + WIFI_OTA_URL_MAX + 4];
    int length = snprintf(payload, sizeof(payload), "%s\n%s\n", ssid, password);
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

static esp_err_t serial_log_post(httpd_req_t *req)
{
    char query[48], enabled[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "enabled", enabled, sizeof(enabled)) != ESP_OK ||
        (strcmp(enabled, "0") != 0 && strcmp(enabled, "1") != 0)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid enabled");
    }
    esp_err_t err = app_config_set_serial_log_enabled(strcmp(enabled, "1") == 0);
    return err == ESP_OK ? send_json(req, "{\"ok\":true}") :
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Serial log save failed");
}

static bool thingspeak_field(const char *form, const char *key, char *out,
                             size_t capacity, bool optional)
{
    char encoded[THINGSPEAK_URL_MAX * 3 + 1];
    esp_err_t err = httpd_query_key_value(form, key, encoded, sizeof(encoded));
    if (optional && err == ESP_ERR_NOT_FOUND) return true;
    if (err != ESP_OK || !form_url_decode(encoded) ||
        strlen(encoded) >= capacity) return false;
    strcpy(out, encoded);
    return true;
}

static esp_err_t thingspeak_get(httpd_req_t *req)
{
    thingspeak_config_t cfg;
    if (thingspeak_config_get(&cfg) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ThingSpeak config unavailable");
    char json[1536];
    size_t pos = 0;
    json[pos++] = '{';
    memcpy(json + pos, "\"login\":", 8); pos += 8;
    if (!json_append_string(json, sizeof(json), &pos, cfg.login))
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ThingSpeak login too long");
    int n = snprintf(json + pos, sizeof(json) - pos, ",\"channel_id\":%lu,\"channel_url\":",
                     (unsigned long)cfg.channel_id);
    if (n < 0 || (size_t)n >= sizeof(json) - pos)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ThingSpeak response too long");
    pos += (size_t)n;
    if (!json_append_string(json, sizeof(json), &pos, cfg.channel_url))
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ThingSpeak URL too long");
    n = snprintf(json + pos, sizeof(json) - pos,
                 ",\"password_configured\":%s,\"read_key_configured\":%s,\"write_key_configured\":%s}",
                 cfg.password[0] ? "true" : "false",
                 cfg.read_api_key[0] ? "true" : "false",
                 cfg.write_api_key[0] ? "true" : "false");
    if (n < 0 || (size_t)n >= sizeof(json) - pos)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ThingSpeak response too long");
    return send_json(req, json);
}

static esp_err_t thingspeak_post(httpd_req_t *req)
{
    char form[1800];
    if (req->content_len <= 0 || (size_t)req->content_len >= sizeof(form))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ThingSpeak form too long");
    size_t received = 0;
    while (received < (size_t)req->content_len) {
        int count = httpd_req_recv(req, form + received, req->content_len - received);
        if (count <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ThingSpeak form incomplete");
        received += (size_t)count;
    }
    form[received] = '\0';
    thingspeak_config_t cfg;
    if (thingspeak_config_get(&cfg) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ThingSpeak config unavailable");
    char channel[16];
    if (!thingspeak_field(form, "login", cfg.login, sizeof(cfg.login), true) ||
        !thingspeak_field(form, "password", cfg.password, sizeof(cfg.password), true) ||
        !thingspeak_field(form, "channel_id", channel, sizeof(channel), false) ||
        !thingspeak_field(form, "read_key", cfg.read_api_key, sizeof(cfg.read_api_key), false) ||
        !thingspeak_field(form, "write_key", cfg.write_api_key, sizeof(cfg.write_api_key), false))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid ThingSpeak fields");
    char *end = NULL;
    errno = 0;
    unsigned long number = strtoul(channel, &end, 10);
    if (errno || end == channel || *end ||
        strspn(channel, "0123456789") != strlen(channel) || number > UINT32_MAX)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid ThingSpeak channel");
    cfg.channel_id = (uint32_t)number;
    esp_err_t err = thingspeak_config_save(&cfg);
    return err == ESP_OK ? send_json(req, "{\"ok\":true}") :
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ThingSpeak save failed");
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

static esp_err_t wifi_auto_update_post(httpd_req_t *req)
{
    char query[64], enabled[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "enabled", enabled, sizeof(enabled)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing auto-update parameter");
    }
    bool value = strcmp(enabled, "1") == 0 || strcmp(enabled, "true") == 0;
    esp_err_t err = wifi_ota_set_auto_update(value);
    if (err == ESP_OK && value) {
        esp_err_t check_err = wifi_ota_check_for_update();
        if (check_err != ESP_OK && check_err != ESP_ERR_INVALID_STATE) err = check_err;
    }
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    return send_json(req, "{\"ok\":true}");
}

static bool json_append_string(char *json, size_t size, size_t *pos, const char *value)
{
    if (!json || !pos || !value || *pos >= size) return false;
    if (*pos + 1 >= size) return false;
    json[(*pos)++] = '"';
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        const char *escape = NULL;
        char unicode[7];
        if (*p == '"') escape = "\\\"";
        else if (*p == '\\') escape = "\\\\";
        else if (*p < 0x20) {
            snprintf(unicode, sizeof(unicode), "\\u%04x", *p);
            escape = unicode;
        }
        if (escape) {
            size_t length = strlen(escape);
            if (*pos + length >= size) return false;
            memcpy(json + *pos, escape, length);
            *pos += length;
        } else {
            if (*pos + 1 >= size) return false;
            json[(*pos)++] = (char)*p;
        }
    }
    if (*pos + 2 > size) return false;
    json[(*pos)++] = '"';
    json[*pos] = '\0';
    return true;
}

static esp_err_t bonds_get(httpd_req_t *req)
{
    ble_phone_bond_t bonds[BLE_PHONE_MAX_BONDS];
    size_t count = ble_phone_get_bonds(bonds, BLE_PHONE_MAX_BONDS);
    const size_t json_size = 4096;
    char *json = malloc(json_size);
    if (!json) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "bond list allocation failed");
    }
    size_t pos = snprintf(json, json_size, "[");
    for (size_t i = 0; i < count; i++) {
        int written = snprintf(json + pos, json_size - pos,
                               "%s{\"address\":\"%s\",\"type\":%u,\"name\":",
                               i ? "," : "", bonds[i].address, bonds[i].address_type);
        if (written < 0 || (size_t)written >= json_size - pos) {
            free(json);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                       "bond list is too large");
        }
        pos += (size_t)written;
        if (!json_append_string(json, json_size, &pos, bonds[i].name) ||
            pos + 2 > json_size) {
            free(json);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                       "bond list is too large");
        }
        json[pos++] = '}';
        json[pos] = '\0';
    }
    if (pos + 2 > json_size) {
        free(json);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "bond list is too large");
    }
    json[pos++] = ']';
    json[pos] = '\0';
    esp_err_t err = send_json(req, json);
    free(json);
    return err;
}

static esp_err_t bonds_delete(httpd_req_t *req)
{
    char query[128], address[24];
    esp_err_t err;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Specify address or all=1");
    esp_err_t parsed = httpd_query_key_value(query, "address", address, sizeof(address));
    if (parsed == ESP_OK && form_url_decode(address) && strlen(address) == 17) {
        err = ble_phone_delete_bond(address);
    } else if (parsed == ESP_ERR_NOT_FOUND && strcmp(query, "all=1") == 0) {
        err = ble_phone_delete_all_bonds();
    } else {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid bond address");
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

static esp_err_t phone_only_route(httpd_req_t *req)
{
    if (wifi_ota_is_runtime_phone_session()) return ESP_OK;
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
}

#define PHONE_ONLY_HANDLER(name) \
    static esp_err_t name##_phone_only(httpd_req_t *req) \
    { \
        esp_err_t access = phone_only_route(req); \
        return access == ESP_OK ? name(req) : access; \
    }

PHONE_ONLY_HANDLER(index_get)
PHONE_ONLY_HANDLER(calibration_get)
PHONE_ONLY_HANDLER(status_get)
PHONE_ONLY_HANDLER(ulp_get)
PHONE_ONLY_HANDLER(ulp_calibration_post)
PHONE_ONLY_HANDLER(config_get)
PHONE_ONLY_HANDLER(config_post)
PHONE_ONLY_HANDLER(serial_log_post)
PHONE_ONLY_HANDLER(thingspeak_get)
PHONE_ONLY_HANDLER(thingspeak_post)
PHONE_ONLY_HANDLER(wifi_get)
PHONE_ONLY_HANDLER(wifi_post)
PHONE_ONLY_HANDLER(wifi_auto_post)
PHONE_ONLY_HANDLER(wifi_auto_update_post)
PHONE_ONLY_HANDLER(registration_get)
PHONE_ONLY_HANDLER(registration_post)
PHONE_ONLY_HANDLER(bonds_get)
PHONE_ONLY_HANDLER(bonds_delete)
PHONE_ONLY_HANDLER(lock_test_post)
PHONE_ONLY_HANDLER(ai_agent_http_restart)
PHONE_ONLY_HANDLER(ai_agent_http_ota)

#undef PHONE_ONLY_HANDLER

esp_err_t web_ui_start(void)
{
    if (s_server) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    /* The Web UI exposes local UI and agent routes (including BLE, ULP, Wi-Fi and OTA
     * controls). Leave a small margin for future routes. */
    config.max_uri_handlers = 32;
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    esp_err_t err = lock_ai_adapter_init();
    if (err != ESP_OK) return err;
    err = httpd_start(&s_server, &config);
    if (err != ESP_OK) return err;
    const httpd_uri_t uris[] = {
        {.uri="/", .method=HTTP_GET, .handler=index_get_phone_only},
        {.uri="/calibration", .method=HTTP_GET, .handler=calibration_get_phone_only},
        {.uri="/api/status", .method=HTTP_GET, .handler=status_get_phone_only},
        {.uri="/api/ulp", .method=HTTP_GET, .handler=ulp_get_phone_only},
        {.uri="/api/ulp/calibration", .method=HTTP_POST, .handler=ulp_calibration_post_phone_only},
        {.uri="/api/config", .method=HTTP_GET, .handler=config_get_phone_only},
        {.uri="/api/config", .method=HTTP_POST, .handler=config_post_phone_only},
        {.uri="/api/config/serial-log", .method=HTTP_POST, .handler=serial_log_post_phone_only},
        {.uri="/api/thingspeak", .method=HTTP_GET, .handler=thingspeak_get_phone_only},
        {.uri="/api/thingspeak", .method=HTTP_POST, .handler=thingspeak_post_phone_only},
        {.uri="/api/wifi", .method=HTTP_GET, .handler=wifi_get_phone_only},
        {.uri="/api/wifi", .method=HTTP_POST, .handler=wifi_post_phone_only},
        {.uri="/api/wifi/auto", .method=HTTP_POST, .handler=wifi_auto_post_phone_only},
        {.uri="/api/wifi/auto-update", .method=HTTP_POST, .handler=wifi_auto_update_post_phone_only},
        {.uri="/api/system/restart", .method=HTTP_POST, .handler=ai_agent_http_restart_phone_only},
        {.uri="/api/ble/registration", .method=HTTP_GET, .handler=registration_get_phone_only},
        {.uri="/api/ble/registration", .method=HTTP_POST, .handler=registration_post_phone_only},
        {.uri="/api/ble/bonds", .method=HTTP_GET, .handler=bonds_get_phone_only},
        {.uri="/api/ble/bonds", .method=HTTP_DELETE, .handler=bonds_delete_phone_only},
        {.uri="/api/lock/test", .method=HTTP_POST, .handler=lock_test_post_phone_only},
        {.uri="/api/ota", .method=HTTP_POST, .handler=ai_agent_http_ota_phone_only},
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
    err = ai_agent_register_http_handlers(s_server);
    if (err != ESP_OK) {
        httpd_stop(s_server);
        s_server = NULL;
        return err;
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
