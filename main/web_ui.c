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

static const char INDEX_HTML[] =
"<!doctype html><html lang='ru'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>BLE Lock</title>"
"<style>body{font-family:Arial;margin:20px;max-width:900px;background:#f3f5f7;color:#20242a}section{background:#fff;padding:16px;margin:12px 0;border-radius:10px}button,input,select{padding:8px;margin:3px}table{width:100%;border-collapse:collapse}td,th{padding:7px;border-bottom:1px solid #ddd;text-align:left}.ok{color:#087b36}.bad{color:#a12622}.muted{color:#777}pre{white-space:pre-wrap}</style></head><body>"
"<h1>BLE Lock</h1>"
"<section><h2>Состояние</h2><div id='status'>Загрузка…</div><button onclick='testOpen()'>Тест открытия</button></section>"
"<section><h2>Настройки замка</h2><table>"
"<tr><td>Текущий RSSI (сейчас)</td><td id='curRssi' class='muted'>—</td></tr>"
"<tr><td>Порог RSSI телефона (открытие), dBm</td><td><input id='pr' type='number' min='-127' max='0'></td></tr>"
"<tr><td>Порог RSSI удержания (слабее), dBm</td><td><input id='prh' type='number' min='-127' max='0'></td></tr>"
"<tr><td>Таймер А, мс</td><td><input id='ta' type='number' min='0'></td></tr>"
"<tr><td>Таймер Б выхода из жёлтого, с</td><td><input id='tb' type='number' min='0' max='3600'></td></tr>"
"<tr><td>Долго открыт, мс</td><td><input id='lo' type='number' min='1000'></td></tr>"
"<tr><td>Уровень GPIO замка при открытии</td><td><select id='ol'><option value='0'>LOW</option><option value='1'>HIGH</option></select></td></tr>"
"<tr><td>GPIO замка</td><td><select id='lg'></select></td></tr>"
"</table><button onclick='saveConfig()'>Сохранить</button><span id='saved'></span></section>"
"<section><h2>BLE</h2><div id='pairing'>Загрузка…</div><button onclick='allowPairing()'>Разрешить регистрацию телефона</button>"
"<p class='muted'>Pairing нового телефона разрешается на 60 секунд. Все зарегистрированные телефоны на это время временно не смогут переподключиться — слот освобождается для нового устройства, затем всё работает как обычно.</p>"
"<h3>Зарегистрированные телефоны</h3><button onclick='loadBonds()'>Обновить</button><button onclick='deleteAllBonds()'>Удалить все bonds</button>"
"<table><thead><tr><th>Identity address</th><th>Тип</th><th></th></tr></thead><tbody id='bonds'></tbody></table></section>"
"<section><h2>OTA</h2><input id='fw' type='file' accept='.bin'><button onclick='uploadFw()'>Загрузить</button><div id='ota'></div></section>"
"<section><h2>События</h2><pre id='events'></pre></section>"
"<script>"
"async function j(u,o){let r=await fetch(u,o);if(!r.ok)throw Error(await r.text());return r.json()}"
"function esc(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/\"/g,'&quot;')}"
"async function loadStatus(){try{let s=await j('/api/status');status.innerHTML=`Версия: ${s.version}<br>Замок: <b class='${s.open?'ok':'bad'}'>${s.open?'OPEN':'LOCKED'}</b><br>Телефон: ${s.connected?'подключён':'не подключён'}<br>Encrypted: ${s.encrypted?'да':'нет'}; Bonded: ${s.bonded?'да':'нет'}<br>RSSI: ${s.rssi===null?'—':s.rssi+' dBm'}<br>Uptime: ${s.uptime_s} с`;curRssi.textContent=s.rssi===null?'—':s.rssi+' dBm'}catch(e){status.textContent=e}}"
"function populateGpioOptions(){let opts='';for(let i=0;i<=48;i++)opts+=`<option value='${i}'>GPIO${i}</option>`;lg.innerHTML=opts}"
"async function loadConfig(){let c=await j('/api/config');pr.value=c.phone_rssi_threshold;prh.value=c.phone_rssi_hold_threshold;lo.value=c.long_open_ms;ta.value=c.timer_a_ms;tb.value=c.timer_b_s;ol.value=c.lock_open_level?1:0;lg.value=c.lock_gpio}"
"async function saveConfig(){let q=new URLSearchParams({phone_rssi_threshold:pr.value,phone_rssi_hold_threshold:prh.value,long_open_ms:lo.value,timer_a_ms:ta.value,timer_b_s:tb.value,lock_gpio:lg.value,lock_open_level:ol.value});try{await j('/api/config?'+q,{method:'POST'});saved.textContent='Сохранено'}catch(e){saved.textContent=e}}"
"async function testOpen(){try{await j('/api/lock/test',{method:'POST'})}catch(e){alert(e)}}"
"async function loadPairing(){try{let p=await j('/api/ble/registration');pairing.innerHTML=p.allowed?`<b class='ok'>Регистрация разрешена: ${p.remaining_s} с</b>`:`<b class='bad'>Регистрация запрещена</b>`}catch(e){pairing.textContent=e}}"
"async function allowPairing(){try{await j('/api/ble/registration',{method:'POST'});await loadPairing()}catch(e){alert(e)}}"
"async function loadBonds(){try{let a=await j('/api/ble/bonds');bonds.innerHTML=a.map(b=>`<tr><td>${b.address}</td><td>${b.type}</td><td><button onclick='deleteBond(${JSON.stringify(b.address)})'>Удалить</button></td></tr>`).join('')||`<tr><td colspan='3' class='muted'>Нет зарегистрированных телефонов</td></tr>`}catch(e){bonds.innerHTML=`<tr><td colspan='3'>${esc(e)}</td></tr>`}}"
"async function deleteBond(a){if(!confirm(`Удалить bond ${a}?`))return;await j('/api/ble/bonds?address='+encodeURIComponent(a),{method:'DELETE'});await loadBonds()}"
"async function deleteAllBonds(){if(!confirm('Удалить все bonds?'))return;await j('/api/ble/bonds',{method:'DELETE'});await loadBonds()}"
"async function loadEvents(){try{events.textContent=(await j('/api/events')).join('\\n')}catch(e){events.textContent=e}}"
"async function uploadFw(){let f=fw.files[0];if(!f)return alert('Выберите .bin');ota.textContent='Загрузка…';let r=await fetch('/api/ota',{method:'POST',body:f});ota.textContent=await r.text()}"
"populateGpioOptions();loadStatus();loadConfig();loadPairing();loadBonds();loadEvents();setInterval(loadStatus,500);setInterval(loadPairing,1000);setInterval(loadBonds,3000);setInterval(loadEvents,3000)"
"</script></body></html>";

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_get(httpd_req_t *req)
{
    ble_phone_status_t status;
    ble_phone_get_status(&status);
    char json[320];
    snprintf(json, sizeof(json),
             "{\"version\":\"%s\",\"open\":%s,\"connected\":%s,"
             "\"encrypted\":%s,\"bonded\":%s,\"rssi\":",
             APP_VERSION, lock_control_is_open() ? "true" : "false",
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

static esp_err_t config_get(httpd_req_t *req)
{
    app_config_t cfg;
    app_config_get(&cfg);
    char json[340];
    snprintf(json, sizeof(json),
             "{\"phone_rssi_threshold\":%d,\"phone_rssi_hold_threshold\":%d,"
             "\"long_open_ms\":%lu,"
             "\"timer_a_ms\":%lu,\"timer_b_s\":%lu,\"lock_gpio\":%d,\"lock_open_level\":%s}",
             cfg.phone_rssi_threshold, cfg.phone_rssi_hold_threshold,
             (unsigned long)cfg.long_open_ms,
             (unsigned long)cfg.timer_a_ms, (unsigned long)cfg.timer_b_s, cfg.lock_gpio,
             cfg.lock_open_level ? "true" : "false");
    return send_json(req, json);
}

static esp_err_t config_post(httpd_req_t *req)
{
    char query[400], rssi[16], rssi_hold[16], long_open[16], timer_a[16], timer_b[16],
         lock_gpio[16], open_level[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "phone_rssi_threshold", rssi, sizeof(rssi)) != ESP_OK ||
        httpd_query_key_value(query, "phone_rssi_hold_threshold", rssi_hold, sizeof(rssi_hold)) != ESP_OK ||
        httpd_query_key_value(query, "long_open_ms", long_open, sizeof(long_open)) != ESP_OK ||
        httpd_query_key_value(query, "timer_a_ms", timer_a, sizeof(timer_a)) != ESP_OK ||
        httpd_query_key_value(query, "timer_b_s", timer_b, sizeof(timer_b)) != ESP_OK ||
        httpd_query_key_value(query, "lock_gpio", lock_gpio, sizeof(lock_gpio)) != ESP_OK ||
        httpd_query_key_value(query, "lock_open_level", open_level, sizeof(open_level)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing parameters");
    }
    esp_err_t err = app_config_set_logic(atoi(rssi), atoi(rssi_hold),
                                         strtoul(long_open, NULL, 10),
                                         strtoul(timer_a, NULL, 10),
                                         strtoul(timer_b, NULL, 10), atoi(lock_gpio),
                                         atoi(open_level) != 0);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, esp_err_to_name(err));
    err = lock_control_set_gpio(atoi(lock_gpio));
    if (err == ESP_OK) err = lock_control_config_changed();
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
    ble_phone_set_registration_mode(true);
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

static esp_err_t events_get(httpd_req_t *req)
{
    char *json = calloc(1, 4096);
    if (!json) return ESP_ERR_NO_MEM;
    event_log_to_json(json, 4096);
    esp_err_t err = send_json(req, json);
    free(json);
    return err;
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
    config.max_uri_handlers = 12;
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) return err;
    const httpd_uri_t uris[] = {
        {.uri="/", .method=HTTP_GET, .handler=index_get},
        {.uri="/api/status", .method=HTTP_GET, .handler=status_get},
        {.uri="/api/config", .method=HTTP_GET, .handler=config_get},
        {.uri="/api/config", .method=HTTP_POST, .handler=config_post},
        {.uri="/api/ble/registration", .method=HTTP_GET, .handler=registration_get},
        {.uri="/api/ble/registration", .method=HTTP_POST, .handler=registration_post},
        {.uri="/api/ble/bonds", .method=HTTP_GET, .handler=bonds_get},
        {.uri="/api/ble/bonds", .method=HTTP_DELETE, .handler=bonds_delete},
        {.uri="/api/lock/test", .method=HTTP_POST, .handler=lock_test_post},
        {.uri="/api/events", .method=HTTP_GET, .handler=events_get},
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
