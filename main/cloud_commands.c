#include "cloud_commands.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "cloud_link.h"
#include "cloud_policy.h"
#include "event_log.h"
#include "lock_control.h"
#include "thingspeak_config.h"
#include "esp_crt_bundle.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"

typedef struct {
    uint32_t version, result_channel, faults;
    cloud_command_t command;
    int64_t finished_utc;
    bool waiting, ack_pending;
    char result[20], state[24];
} command_record_t;
typedef struct { bool result; char data[1025]; } message_t;

static command_record_t s_record;
static QueueHandle_t s_messages;
static esp_mqtt_client_handle_t s_client;
static atomic_bool s_connected, s_busy;
static _Atomic(const char *) s_status = "disabled";
static char s_mac[18], s_command_topic[64], s_result_topic[64];
static message_t s_fragment;
static size_t s_fragment_size;
static bool s_fragment_valid;
static int64_t s_dispatched_us, s_last_publish;
static bool s_dirty;

bool cloud_commands_connected(void) { return atomic_load(&s_connected); }
bool cloud_commands_busy(void) { return atomic_load(&s_busy); }
const char *cloud_commands_status(void) { return s_status; }

static int64_t now_s(void)
{
    struct timeval tv; gettimeofday(&tv, NULL); return tv.tv_sec;
}

static esp_err_t save_record(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("cloud_command", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, "last", &s_record, sizeof(s_record));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) s_status = "command_nvs";
    return err;
}

static void mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    esp_mqtt_event_handle_t event = data;
    if (id == MQTT_EVENT_CONNECTED) {
        atomic_store(&s_connected, true);
        if (!atomic_load(&s_busy)) s_status = "connected";
        esp_mqtt_client_subscribe(event->client, s_command_topic, 0);
        esp_mqtt_client_subscribe(event->client, s_result_topic, 0);
    } else if (id == MQTT_EVENT_DISCONNECTED || id == MQTT_EVENT_ERROR) {
        atomic_store(&s_connected, false);
        if (!atomic_load(&s_busy)) s_status = "mqtt_disconnected";
        s_fragment_valid = false;
    } else if (id == MQTT_EVENT_DATA) {
        if (event->current_data_offset == 0) {
            s_fragment_size = 0;
            bool command = event->topic && event->topic_len == (int)strlen(s_command_topic) &&
                           !memcmp(event->topic, s_command_topic, event->topic_len);
            bool result = event->topic && event->topic_len == (int)strlen(s_result_topic) &&
                          !memcmp(event->topic, s_result_topic, event->topic_len);
            s_fragment.result = result;
            s_fragment_valid = (command || result) && event->total_data_len > 0 && event->total_data_len <= 1024;
        }
        if (!s_fragment_valid || event->data_len <= 0 ||
            event->current_data_offset != (int)s_fragment_size ||
            (size_t)event->data_len > 1024 - s_fragment_size) { s_fragment_valid = false; return; }
        /* Embedded NUL must not truncate a command into a different valid JSON. */
        if (memchr(event->data, 0, event->data_len)) { s_fragment_valid = false; return; }
        memcpy(s_fragment.data + s_fragment_size, event->data, event->data_len);
        s_fragment_size += event->data_len;
        if (s_fragment_size == (size_t)event->total_data_len) {
            s_fragment.data[s_fragment_size] = '\0';
            xQueueSend(s_messages, &s_fragment, 0);
            s_fragment_valid = false;
        }
    }
}

static void finish(const char *result)
{
    lock_feedback_t feedback; lock_control_get_feedback(&feedback);
    snprintf(s_record.result, sizeof(s_record.result), "%s", result);
    snprintf(s_record.state, sizeof(s_record.state), "%s", lock_control_feedback_name(&feedback));
    s_record.faults = feedback.fault_flags;
    s_record.finished_utc = cloud_link_time_ready() ? now_s() : 0;
    s_record.waiting = false;
    s_record.ack_pending = true;
    atomic_store(&s_busy, true);
    s_dirty = save_record() != ESP_OK;
    s_status = s_dirty ? "command_nvs" : "result_pending";
    char detail[48];
    snprintf(detail, sizeof(detail), "%" PRIu64 ":%s", s_record.command.id, result);
    cloud_link_record("command_result", "cloud", s_record.command.actor, detail);
}

static void accept(const char *payload, const thingspeak_config_t *cfg)
{
    cloud_command_t command;
    if (!cloud_command_parse(payload, &command)) { s_status = "invalid_command"; return; }
    if (strcmp(command.target, s_mac)) { s_status = "wrong_target"; return; }
    if (command.id == s_record.command.id && command.id) {
        /* Never change result, deadline or operation when a sender retries an ID. */
        if (!s_record.waiting) {
            s_record.ack_pending = true;
            atomic_store(&s_busy, true);
            s_dirty = save_record() != ESP_OK;
        }
        return;
    }
    if (!cfg->enabled || !cfg->commands_enabled ||
        !cloud_command_valid(&command, s_mac, s_record.command.id, now_s(), cloud_link_time_ready())) {
        s_status = "stale_or_unsynced"; return;
    }
    if (s_record.waiting || s_record.ack_pending || s_dirty) { s_status = "busy"; return; }
    command_record_t previous = s_record;
    atomic_store(&s_busy, true);
    s_record = (command_record_t){.version = 1, .command = command,
                                 .result_channel = cfg->result_channel_id, .waiting = true};
    snprintf(s_record.result, sizeof(s_record.result), "%s", "pending");
    if (save_record() != ESP_OK) {
        s_record = previous;
        atomic_store(&s_busy, previous.waiting || previous.ack_pending);
        return;
    }
    lock_feedback_t feedback; lock_control_get_feedback(&feedback);
    if (!feedback.valid || feedback.fault_flags || feedback.close_rejected ||
        feedback.calibrating || feedback.calibration_required) { finish("fault"); return; }
    if (command.operation == CLOUD_CLOSE && feedback.hold_open) { finish("hold_conflict"); return; }
    static const char *operations[] = {"manual_open", "close", "hold_on", "hold_off"};
    s_dispatched_us = esp_timer_get_time();
    int64_t expires_us = s_dispatched_us + (command.expires - now_s()) * 1000000;
    if (lock_request_cloud(operations[command.operation], command.actor, command.id, expires_us) != ESP_OK)
        finish("queue_failed");
    else s_status = "waiting_feedback";
}

static void check_feedback(void)
{
    if (!s_record.waiting || s_dirty) return;
    lock_feedback_t feedback; lock_control_get_feedback(&feedback);
    int64_t elapsed = esp_timer_get_time() - s_dispatched_us;
    if (feedback.sampled_at_us <= s_dispatched_us || feedback.cloud_command_id != s_record.command.id) {
        if (elapsed >= 5000000) finish("timeout");
        return;
    }
    if (feedback.fault_flags || feedback.close_rejected || feedback.calibrating || feedback.calibration_required) {
        finish("fault"); return;
    }
    if ((s_record.command.operation == CLOUD_HOLD_ON || s_record.command.operation == CLOUD_HOLD_OFF) &&
        !feedback.mode_persisted) { finish("mode_nvs"); return; }
    bool confirmed = feedback.valid;
    switch (s_record.command.operation) {
        case CLOUD_OPEN: confirmed &= feedback.requested_open && feedback.state == LOCK_STATE_UNLOCKED; break;
        case CLOUD_CLOSE: confirmed &= !feedback.requested_open && feedback.state == LOCK_STATE_LOCKED && !feedback.hold_open; break;
        case CLOUD_HOLD_ON: confirmed &= feedback.hold_open && feedback.requested_open && feedback.state == LOCK_STATE_UNLOCKED; break;
        case CLOUD_HOLD_OFF: confirmed &= !feedback.hold_open; break;
    }
    if (confirmed) finish("ok");
    else if (elapsed >= 5000000) finish("timeout");
}

static bool equal_field(cJSON *json, const char *key, const char *expected)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    return cJSON_IsString(item) && !strcmp(item->valuestring, expected);
}

static void check_result(const char *payload)
{
    if (!s_record.ack_pending || s_record.waiting || s_dirty) return;
    cJSON *json = cJSON_Parse(payload);
    char id[24], faults[16], timestamp[24];
    snprintf(id, sizeof(id), "%" PRIu64, s_record.command.id);
    snprintf(faults, sizeof(faults), "%" PRIu32, s_record.faults);
    snprintf(timestamp, sizeof(timestamp), "%" PRId64, s_record.finished_utc);
    bool delivered = equal_field(json, "field1", id) && equal_field(json, "field2", s_record.result) &&
                     equal_field(json, "field3", s_record.state) && equal_field(json, "field4", s_mac) &&
                     equal_field(json, "field5", timestamp) && equal_field(json, "field6", faults);
    cJSON_Delete(json);
    if (delivered) {
        s_record.ack_pending = false;
        s_dirty = save_record() != ESP_OK;
        if (!s_dirty) { atomic_store(&s_busy, false); s_status = "ready"; }
    }
}

static void publish_result(void)
{
    if (!s_client || !cloud_commands_connected() || !s_record.ack_pending || s_record.waiting ||
        s_dirty || !cloud_link_time_ready()) return;
    int64_t now = esp_timer_get_time();
    if (s_last_publish && now - s_last_publish < 16000000) return;
    s_last_publish = now;
    char topic[64], payload[256];
    snprintf(topic, sizeof(topic), "channels/%" PRIu32 "/publish", s_record.result_channel);
    snprintf(payload, sizeof(payload), "field1=%" PRIu64 "&field2=%s&field3=%s&field4=%s&field5=%" PRId64 "&field6=%" PRIu32,
             s_record.command.id, s_record.result, s_record.state, s_mac, s_record.finished_utc, s_record.faults);
    esp_mqtt_client_publish(s_client, topic, payload, 0, 0, 0);
}

static void stop_client(void)
{
    if (s_client) { esp_mqtt_client_stop(s_client); esp_mqtt_client_destroy(s_client); s_client = NULL; }
    atomic_store(&s_connected, false);
    s_fragment_valid = false;
    xQueueReset(s_messages);
}

static void command_task(void *arg)
{
    (void)arg;
    thingspeak_config_t active = {0};
    int64_t retry_at = 0;
    for (;;) {
        thingspeak_config_t cfg;
        bool configured = thingspeak_config_get(&cfg) == ESP_OK && cfg.enabled && cfg.commands_enabled;
        if (!configured) {
            stop_client();
            s_status = "disabled";
        } else {
            if (!cloud_link_time_ready()) s_status = "waiting_time";
            bool changed = active.command_channel_id != cfg.command_channel_id ||
                           active.result_channel_id != cfg.result_channel_id ||
                           strcmp(active.mqtt_client_id, cfg.mqtt_client_id) ||
                           strcmp(active.login, cfg.login) || strcmp(active.password, cfg.password);
            if (changed) { stop_client(); active = cfg; retry_at = 0; }
            if (!s_client && cloud_link_time_ready() && esp_timer_get_time() >= retry_at) {
                snprintf(s_command_topic, sizeof(s_command_topic), "channels/%" PRIu32 "/subscribe", cfg.command_channel_id);
                snprintf(s_result_topic, sizeof(s_result_topic), "channels/%" PRIu32 "/subscribe", cfg.result_channel_id);
                esp_mqtt_client_config_t mqtt = {
                    .broker.address.uri = "mqtts://mqtt3.thingspeak.com:8883",
                    .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
                    .credentials.client_id = active.mqtt_client_id, .credentials.username = active.login,
                    .credentials.authentication.password = active.password,
                    .session.protocol_ver = MQTT_PROTOCOL_V_3_1_1, .session.keepalive = 60,
                    .network.reconnect_timeout_ms = 30000, .network.timeout_ms = 8000,
                    .task.priority = 2, .task.stack_size = 6144, .buffer.size = 1536,
                };
                retry_at = esp_timer_get_time() + 30000000;
                s_client = esp_mqtt_client_init(&mqtt);
                if (s_client) {
                    esp_err_t err = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event, NULL);
                    if (err == ESP_OK) err = esp_mqtt_client_start(s_client);
                    if (err != ESP_OK) { esp_mqtt_client_destroy(s_client); s_client = NULL; }
                }
                s_status = "connecting";
            }
        }
        if (s_dirty && save_record() == ESP_OK) {
            s_dirty = false;
            atomic_store(&s_busy, s_record.waiting || s_record.ack_pending);
        }
        message_t message;
        while (xQueueReceive(s_messages, &message, 0) == pdTRUE) {
            if (message.result) check_result(message.data);
            else if (configured) accept(message.data, &cfg);
        }
        check_feedback();
        if (configured) publish_result();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t cloud_commands_init(void)
{
    if (!cloud_policy_selftest()) {
        event_log_add("Cloud policy selftest: FAIL"); return ESP_FAIL;
    }
    event_log_add("Cloud policy selftest: PASS");
    s_record = (command_record_t){.version = 1};
    nvs_handle_t h;
    esp_err_t err = nvs_open("cloud_command", NVS_READONLY, &h);
    if (err == ESP_OK) {
        size_t size = sizeof(s_record);
        err = nvs_get_blob(h, "last", &s_record, &size);
        nvs_close(h);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
        if (s_record.version != 1 || s_record.command.operation > CLOUD_HOLD_OFF) return ESP_ERR_INVALID_STATE;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) return err;
    uint8_t mac[6];
    err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) return err;
    snprintf(s_mac, sizeof(s_mac), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    s_messages = xQueueCreate(4, sizeof(message_t));
    if (!s_messages) return ESP_ERR_NO_MEM;
    if (s_record.waiting) finish("interrupted");
    atomic_store(&s_busy, s_record.waiting || s_record.ack_pending);
    return ESP_OK;
}

esp_err_t cloud_commands_start(void)
{
    if (!s_messages) return ESP_ERR_INVALID_STATE;
    return xTaskCreate(command_task, "cloud_commands", 6144, NULL, 2, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
