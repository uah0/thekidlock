#include "cloud_link.h"
#include "cloud_commands.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "cJSON.h"
#include "event_log.h"
#include "thingspeak_config.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"

#define JOURNAL_SIZE 32
#define JOURNAL_VERSION 1
#define CLOUD_INTERVAL_US 16000000LL

typedef struct {
    uint64_t id;
    int64_t utc_us, monotonic_us, delivery_s;
    uint32_t boot;
    char event[24], source[12], actor[18], result[48];
} cloud_event_t;

typedef struct {
    uint32_t version, count, dropped, channel;
    uint64_t next_id;
    int64_t delivery_s;
    cloud_event_t events[JOURNAL_SIZE];
} journal_t;

static journal_t s_journal;
static QueueHandle_t s_events;
static atomic_uint s_pending, s_dropped;
static atomic_bool s_enabled, s_time_ready;
static int64_t s_anchor_us;
static uint32_t s_boot;
static char s_mac[18];
static _Atomic(const char *) s_error = "disabled";
static int64_t s_last_upload;
static int64_t s_last_drop_save;

unsigned cloud_link_pending(void) {
    return atomic_load(&s_pending) + (s_events ? uxQueueMessagesWaiting(s_events) : 0);
}
unsigned cloud_link_dropped(void) { return atomic_load(&s_dropped); }
bool cloud_link_time_ready(void) { return atomic_load(&s_time_ready); }
bool cloud_link_mqtt_ready(void) { return cloud_commands_connected(); }
const char *cloud_link_error(void) { return s_error; }

static int64_t utc_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

static void time_synced(struct timeval *tv)
{
    (void)tv;
    if (!atomic_load(&s_time_ready)) {
        s_anchor_us = utc_us() - esp_timer_get_time();
        atomic_store(&s_time_ready, true);
    }
}

void cloud_link_record(const char *event, const char *source, const char *actor, const char *result)
{
    if (!s_events || !atomic_load(&s_enabled)) return;
    cloud_event_t item = {.monotonic_us = esp_timer_get_time(), .boot = s_boot};
    if (cloud_link_time_ready()) item.utc_us = utc_us();
    snprintf(item.event, sizeof(item.event), "%s", event ? event : "unknown");
    snprintf(item.source, sizeof(item.source), "%s", source ? source : "system");
    snprintf(item.actor, sizeof(item.actor), "%s", actor ? actor : "");
    snprintf(item.result, sizeof(item.result), "%s", result ? result : "");
    if (xQueueSend(s_events, &item, 0) != pdTRUE) atomic_fetch_add(&s_dropped, 1);
}

static esp_err_t journal_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("cloud_journal", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    s_journal.dropped = atomic_load(&s_dropped);
    size_t length = offsetof(journal_t, events) + s_journal.count * sizeof(cloud_event_t);
    err = nvs_set_blob(h, "journal", &s_journal, length);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) s_error = "journal_nvs";
    return err;
}

typedef struct { char *data; size_t used, capacity; bool overflow; } response_t;
static esp_err_t http_event(esp_http_client_event_t *event)
{
    response_t *r = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && r) {
        if (event->data_len < 0 || (size_t)event->data_len >= r->capacity - r->used) {
            r->overflow = true;
            return ESP_FAIL;
        }
        memcpy(r->data + r->used, event->data, event->data_len);
        r->used += event->data_len;
        r->data[r->used] = '\0';
    }
    return ESP_OK;
}

static cJSON *https_json(const char *url, const char *body, size_t capacity, int *status)
{
    char *buffer = calloc(1, capacity);
    if (!buffer) return NULL;
    response_t response = {.data = buffer, .capacity = capacity};
    esp_http_client_config_t config = {
        .url = url, .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 8000, .event_handler = http_event, .user_data = &response,
        .disable_auto_redirect = true, .buffer_size = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { free(buffer); return NULL; }
    if (body) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, strlen(body));
    }
    esp_err_t err = esp_http_client_perform(client);
    *status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    cJSON *json = err == ESP_OK && !response.overflow ? cJSON_Parse(buffer) : NULL;
    free(buffer);
    return json;
}

static void event_fields(const cloud_event_t *event, char fields[8][64])
{
    snprintf(fields[0], 64, "%s", s_mac);
    snprintf(fields[1], 64, "%s", event->actor);
    snprintf(fields[2], 64, "%s", event->event);
    snprintf(fields[3], 64, "%s", event->source);
    if (event->utc_us) snprintf(fields[4], 64, "%" PRId64, event->utc_us);
    else snprintf(fields[4], 64, "unsynced");
    snprintf(fields[5], 64, "%s/%08" PRIx32 "/%" PRIu64, s_mac, event->boot, event->id);
    snprintf(fields[6], 64, "%s", event->result);
    snprintf(fields[7], 64, "boot=%08" PRIx32 ";uptime_us=%" PRId64, event->boot, event->monotonic_us);
}

/* A lost response is not permission to remove records. Reconcile stable IDs. */
static uint32_t verify_delivery(const thingspeak_config_t *cfg, unsigned count)
{
    char url[256];
    snprintf(url, sizeof(url), "https://api.thingspeak.com/channels/%" PRIu32 "/feeds.json?api_key=%s&results=32",
             cfg->channel_id, cfg->read_api_key);
    int status = 0;
    cJSON *json = https_json(url, NULL, 24576, &status);
    cJSON *feeds = cJSON_GetObjectItemCaseSensitive(json, "feeds");
    uint32_t delivered = 0;
    for (unsigned i = 0; status == 200 && cJSON_IsArray(feeds) && i < count; ++i) {
        char fields[8][64]; event_fields(&s_journal.events[i], fields);
        bool found = false;
        cJSON *feed;
        cJSON_ArrayForEach(feed, feeds) {
            bool equal = true;
            for (unsigned n = 0; equal && n < 8; ++n) {
                char key[8]; snprintf(key, sizeof(key), "field%u", n + 1);
                cJSON *value = cJSON_GetObjectItemCaseSensitive(feed, key);
                equal = (cJSON_IsString(value) && strcmp(value->valuestring, fields[n]) == 0) ||
                        (!fields[n][0] && cJSON_IsNull(value));
            }
            if (equal) { found = true; break; }
        }
        if (found) delivered |= UINT32_C(1) << i;
    }
    cJSON_Delete(json);
    return delivered;
}

static bool network_ready(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info;
    return netif && esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr;
}

static void upload(const thingspeak_config_t *cfg)
{
    if (!cfg->enabled || !cfg->channel_id || !s_journal.count || !cloud_link_time_ready() || !network_ready()) return;
    int64_t mono = esp_timer_get_time();
    if (s_last_upload && mono - s_last_upload < CLOUD_INTERVAL_US) return;
    s_last_upload = mono;
    int64_t now = utc_us() / 1000000;
    unsigned count = s_journal.count;
    bool changed = false;
    for (unsigned i = 0; i < count; ++i) {
        cloud_event_t *event = &s_journal.events[i];
        if (!event->utc_us && event->boot == s_boot) {
            event->utc_us = s_anchor_us + event->monotonic_us;
            changed = true;
        }
        if (!event->delivery_s) {
            int64_t slot = now - (int64_t)(count - i) + 1;
            if (slot <= s_journal.delivery_s) slot = s_journal.delivery_s + 1;
            if (slot > now) { count = i; break; }
            event->delivery_s = slot;
            s_journal.delivery_s = slot;
            changed = true;
        }
    }
    /* Retry persistence even if a previous save failed after updating RAM. */
    if ((changed || count) && journal_save() != ESP_OK) return;
    if (!count) return;
    cJSON *root = cJSON_CreateObject(), *updates = cJSON_CreateArray();
    if (!root || !updates) { cJSON_Delete(root); cJSON_Delete(updates); return; }
    cJSON_AddStringToObject(root, "write_api_key", cfg->write_api_key);
    cJSON_AddItemToObject(root, "updates", updates);
    for (unsigned i = 0; i < count; ++i) {
        char fields[8][64]; event_fields(&s_journal.events[i], fields);
        cJSON *item = cJSON_CreateObject();
        if (!item) { cJSON_Delete(root); return; }
        char created_at[24];
        snprintf(created_at, sizeof(created_at), "%" PRId64, s_journal.events[i].delivery_s);
        cJSON_AddStringToObject(item, "created_at", created_at);
        for (unsigned n = 0; n < 8; ++n) {
            char key[8]; snprintf(key, sizeof(key), "field%u", n + 1);
            cJSON_AddStringToObject(item, key, fields[n]);
        }
        cJSON_AddItemToArray(updates, item);
    }
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return;
    char url[128];
    snprintf(url, sizeof(url), "https://api.thingspeak.com/channels/%" PRIu32 "/bulk_update.json", cfg->channel_id);
    int status = 0;
    cJSON *reply = https_json(url, body, 2048, &status);
    free(body);
    bool success = status == 200 && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(reply, "success"));
    cJSON_Delete(reply);
    /* Bulk can reject existing timestamps individually. Verify actual records even on 200. */
    uint32_t delivered = verify_delivery(cfg, count);
    if (delivered) {
        journal_t *backup = malloc(sizeof(*backup));
        if (!backup) return;
        *backup = s_journal;
        unsigned kept = 0;
        for (unsigned i = 0; i < s_journal.count; ++i) {
            if (i < count && (delivered & (UINT32_C(1) << i))) continue;
            s_journal.events[kept++] = s_journal.events[i];
        }
        s_journal.count = kept;
        if (journal_save() != ESP_OK) s_journal = *backup;
        else s_error = success ? "ok" : "delivery_reconciled";
        free(backup);
        atomic_store(&s_pending, s_journal.count);
    } else s_error = "https_delivery";
}

static void cloud_task(void *arg)
{
    (void)arg;
    bool sntp_started = false;
    bool dirty = false;
    for (;;) {
        thingspeak_config_t cfg;
        if (thingspeak_config_get(&cfg) != ESP_OK) {
            s_error = "config_nvs";
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        atomic_store(&s_enabled, cfg.enabled);
        if (cfg.enabled && network_ready() && !sntp_started) {
            esp_sntp_config_t time_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("time.cloudflare.com");
            time_config.sync_cb = time_synced;
            sntp_started = esp_netif_sntp_init(&time_config) == ESP_OK;
        }
        cloud_event_t item;
        while (xQueueReceive(s_events, &item, 0) == pdTRUE) {
            if (s_journal.count == JOURNAL_SIZE) { atomic_fetch_add(&s_dropped, 1); continue; }
            item.id = s_journal.next_id++;
            s_journal.events[s_journal.count++] = item;
            dirty = true;
        }
        if (!s_journal.channel && s_journal.count) { s_journal.channel = cfg.channel_id; dirty = true; }
        if (!s_journal.count && s_journal.channel != cfg.channel_id) {
            s_journal.channel = cfg.channel_id; s_journal.delivery_s = 0; dirty = true;
        }
        atomic_store(&s_pending, s_journal.count);
        if (dirty && journal_save() == ESP_OK) dirty = false;
        /* A full offline journal must not rewrite its entire blob per dropped event. */
        int64_t now = esp_timer_get_time();
        if (!dirty && s_journal.dropped != atomic_load(&s_dropped) && now - s_last_drop_save >= 60000000) {
            nvs_handle_t h;
            unsigned dropped = atomic_load(&s_dropped);
            if (nvs_open("cloud_journal", NVS_READWRITE, &h) == ESP_OK) {
                if (nvs_set_u32(h, "dropped", dropped) == ESP_OK && nvs_commit(h) == ESP_OK) {
                    s_journal.dropped = dropped; s_last_drop_save = now;
                }
                nvs_close(h);
            }
        }
        if (cfg.channel_id != s_journal.channel && s_journal.count) s_error = "journal_channel_mismatch";
        else if (!dirty) upload(&cfg);
        if (!cfg.enabled) s_error = "disabled";
        else if (!cloud_link_time_ready()) s_error = "waiting_time";
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t cloud_link_init(void)
{
    if (s_events) return ESP_OK;
    thingspeak_config_t cfg;
    esp_err_t err = thingspeak_config_get(&cfg);
    if (err != ESP_OK) return err;
    err = cloud_commands_init();
    if (err != ESP_OK) return err;
    memset(&s_journal, 0, sizeof(s_journal));
    s_journal.version = JOURNAL_VERSION; s_journal.next_id = 1;
    nvs_handle_t h;
    err = nvs_open("cloud_journal", NVS_READONLY, &h);
    if (err == ESP_OK) {
        size_t length = sizeof(s_journal);
        err = nvs_get_blob(h, "journal", &s_journal, &length);
        uint32_t dropped = 0;
        if (nvs_get_u32(h, "dropped", &dropped) == ESP_OK && dropped > s_journal.dropped)
            s_journal.dropped = dropped;
        nvs_close(h);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
        if (s_journal.version != JOURNAL_VERSION || s_journal.count > JOURNAL_SIZE || !s_journal.next_id)
            return ESP_ERR_INVALID_STATE;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) return err;
    uint8_t mac[6];
    err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) return err;
    snprintf(s_mac, sizeof(s_mac), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    s_boot = esp_random();
    atomic_store(&s_pending, s_journal.count);
    atomic_store(&s_dropped, s_journal.dropped);
    atomic_store(&s_enabled, cfg.enabled);
    s_events = xQueueCreate(JOURNAL_SIZE, sizeof(cloud_event_t));
    return s_events ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t cloud_link_start(void)
{
    if (!s_events) return ESP_ERR_INVALID_STATE;
    esp_err_t err = cloud_commands_start();
    if (err != ESP_OK) return err;
    return xTaskCreate(cloud_task, "cloud_link", 8192, NULL, 2, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
