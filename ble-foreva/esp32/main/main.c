#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#define COMPANY_ID 0xFFFF
#define SCAN_UNITS 160 /* 160 * 0.625 ms = 100 ms; window equals interval. */
#define STAT_PERIOD_MS 10000
#define RING_SECONDS 60

typedef enum { MODE_STAT, MODE_RAW, MODE_CSV } output_mode_t;
typedef struct {
    uint64_t second;
    uint32_t count;
} second_bucket_t;
typedef struct {
    bool seen;
    uint32_t device, session;
    uint64_t first_ms, last_ms, total, gap_sum_ms, gap_count, gap_max_ms, gap_last_ms;
    uint64_t missing_2s, missing_5s, missing_10s, rssi_sum_abs, dropped_rows;
    int rssi_current, rssi_min, rssi_max;
    second_bucket_t ring[RING_SECONDS];
} stats_t;
typedef struct {
    uint64_t ms, gap_ms;
    uint32_t device, session;
    int rssi;
    char address[18];
    char payload[63];
    bool session_changed;
} packet_row_t;

static stats_t stats;
static portMUX_TYPE stats_lock = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t rows;
static volatile output_mode_t mode = MODE_STAT;

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void scan_start(void);

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        printf("# SCAN_RESTART reason=%d boot_ms=%" PRIu64 "\n",
               event->disc_complete.reason, (uint64_t)(esp_timer_get_time() / 1000));
        scan_start();
        return 0;
    }
    if (event->type != BLE_GAP_EVENT_DISC) return 0;
    const struct ble_gap_disc_desc *disc = &event->disc;
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) != 0) return 0;
    const uint8_t *m = fields.mfg_data;
    if (!m || fields.mfg_data_len != 15 || m[0] != (COMPANY_ID & 0xff) ||
        m[1] != (COMPANY_ID >> 8) || memcmp(m + 2, "FORE", 4) != 0 || m[6] != 1) return 0;

    packet_row_t row = {0};
    row.ms = (uint64_t)(esp_timer_get_time() / 1000);
    row.device = read_be32(m + 7);
    row.session = read_be32(m + 11);
    row.rssi = disc->rssi;
    snprintf(row.address, sizeof(row.address), "%02X:%02X:%02X:%02X:%02X:%02X",
             disc->addr.val[5], disc->addr.val[4], disc->addr.val[3],
             disc->addr.val[2], disc->addr.val[1], disc->addr.val[0]);
    for (int i = 0; i < disc->length_data && i < 31; ++i)
        snprintf(row.payload + i * 2, sizeof(row.payload) - i * 2, "%02X", disc->data[i]);

    portENTER_CRITICAL(&stats_lock);
    if (!stats.seen || stats.device != row.device || stats.session != row.session) {
        memset(&stats, 0, sizeof(stats));
        stats.seen = true;
        stats.device = row.device;
        stats.session = row.session;
        stats.first_ms = row.ms;
        stats.rssi_min = row.rssi;
        stats.rssi_max = row.rssi;
        row.session_changed = true;
    } else {
        row.gap_ms = row.ms - stats.last_ms;
        stats.gap_last_ms = row.gap_ms;
        stats.gap_sum_ms += row.gap_ms;
        stats.gap_count++;
        if (row.gap_ms > stats.gap_max_ms) stats.gap_max_ms = row.gap_ms;
        if (row.gap_ms > 2000) stats.missing_2s++;
        if (row.gap_ms > 5000) stats.missing_5s++;
        if (row.gap_ms > 10000) stats.missing_10s++;
    }
    stats.last_ms = row.ms;
    stats.total++;
    stats.rssi_current = row.rssi;
    if (row.rssi < stats.rssi_min) stats.rssi_min = row.rssi;
    if (row.rssi > stats.rssi_max) stats.rssi_max = row.rssi;
    stats.rssi_sum_abs += (uint64_t)(-row.rssi);
    second_bucket_t *bucket = &stats.ring[(row.ms / 1000) % RING_SECONDS];
    if (bucket->second != row.ms / 1000) { bucket->second = row.ms / 1000; bucket->count = 0; }
    bucket->count++;
    portEXIT_CRITICAL(&stats_lock);
    if (xQueueSend(rows, &row, 0) != pdTRUE) {
        portENTER_CRITICAL(&stats_lock); stats.dropped_rows++; portEXIT_CRITICAL(&stats_lock);
    }
    return 0;
}

static void scan_start(void)
{
    uint8_t own_addr_type;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) { printf("# SCAN_ERROR address rc=%d\n", rc); return; }
    struct ble_gap_disc_params p = {0};
    p.passive = 1;
    p.filter_duplicates = 0;
    p.itvl = SCAN_UNITS;
    p.window = SCAN_UNITS;
    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &p, gap_event, NULL);
    printf("# SCAN_START rc=%d interval_units=%d window_units=%d passive=1 duplicates=0\n",
           rc, SCAN_UNITS, SCAN_UNITS);
}

static void on_sync(void) { scan_start(); }
static void host_task(void *arg) { (void)arg; nimble_port_run(); nimble_port_freertos_deinit(); }

static void output_task(void *arg)
{
    (void)arg;
    packet_row_t row;
    while (xQueueReceive(rows, &row, portMAX_DELAY) == pdTRUE) {
        if (row.session_changed)
            printf("# SESSION boot_ms=%" PRIu64 " device=%08" PRIX32 " session=%08" PRIX32 "\n",
                   row.ms, row.device, row.session);
        if (mode == MODE_RAW)
            printf("%" PRIu64 " DEV=%08" PRIX32 " SES=%08" PRIX32 " RSSI=%d GAP_MS=%" PRIu64
                   " ADDR=%s PAYLOAD=%s\n", row.ms, row.device, row.session, row.rssi,
                   row.gap_ms, row.address, row.payload);
        else if (mode == MODE_CSV)
            printf("%" PRIu64 ",%08" PRIX32 ",%08" PRIX32 ",%d,%" PRIu64 ",%s,%s\n",
                   row.ms, row.device, row.session, row.rssi, row.gap_ms, row.address, row.payload);
    }
}

static void stat_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(STAT_PERIOD_MS));
        if (mode != MODE_STAT) continue;
        stats_t s;
        portENTER_CRITICAL(&stats_lock); memcpy(&s, &stats, sizeof(s)); portEXIT_CRITICAL(&stats_lock);
        uint64_t now = (uint64_t)(esp_timer_get_time() / 1000);
        if (!s.seen) { printf("BLE FOREVA STAT boot_ms=%" PRIu64 " no matching packets\n", now); continue; }
        uint32_t last_60 = 0;
        for (int i = 0; i < RING_SECONDS; ++i)
            if (now / 1000 >= s.ring[i].second && now / 1000 - s.ring[i].second < 60)
                last_60 += s.ring[i].count;
        uint64_t silence = now - s.last_ms;
        printf("BLE FOREVA STAT boot_ms=%" PRIu64 "\n"
               "Device=%08" PRIX32 " Session=%08" PRIX32 " First_ms=%" PRIu64 " Last_ms=%" PRIu64 "\n"
               "Packets_total=%" PRIu64 " Packets_last_60s=%" PRIu32 " Dropped_serial_rows=%" PRIu64 "\n"
               "RSSI_current=%d min=%d max=%d avg=%.1f\n"
               "Gap_last_ms=%" PRIu64 " avg_ms=%.1f max_observed_ms=%" PRIu64
               " current_silence_ms=%" PRIu64 " max_including_silence_ms=%" PRIu64 "\n"
               "Missing_gt_2s=%" PRIu64 " gt_5s=%" PRIu64 " gt_10s=%" PRIu64 "\n",
               now, s.device, s.session, s.first_ms, s.last_ms, s.total, last_60, s.dropped_rows,
               s.rssi_current, s.rssi_min, s.rssi_max, -(double)s.rssi_sum_abs / s.total,
               s.gap_last_ms, s.gap_count ? (double)s.gap_sum_ms / s.gap_count : 0.0,
               s.gap_max_ms, silence, silence > s.gap_max_ms ? silence : s.gap_max_ms,
               s.missing_2s, s.missing_5s, s.missing_10s);
    }
}

static void command_task(void *arg)
{
    (void)arg;
    char line[32];
    while (fgets(line, sizeof(line), stdin)) {
        if (strncmp(line, "RAW", 3) == 0) mode = MODE_RAW;
        else if (strncmp(line, "CSV", 3) == 0) {
            mode = MODE_CSV;
            printf("timestamp_ms,device_id,session_id,rssi,gap_ms,address,payload_hex\n");
        } else if (strncmp(line, "STAT", 4) == 0) mode = MODE_STAT;
        else { printf("# Commands: STAT, RAW, CSV\n"); continue; }
        printf("# MODE=%s\n", mode == MODE_RAW ? "RAW" : mode == MODE_CSV ? "CSV" : "STAT");
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    esp_err_t rc = nvs_flash_init();
    if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase()); rc = nvs_flash_init();
    }
    ESP_ERROR_CHECK(rc);
    rows = xQueueCreate(256, sizeof(packet_row_t));
    ESP_ERROR_CHECK(rows ? ESP_OK : ESP_ERR_NO_MEM);
    printf("# BOOT BLE_FOREVA boot_ms=%" PRIu64 " mode=STAT\n", (uint64_t)(esp_timer_get_time() / 1000));
    xTaskCreate(output_task, "foreva_output", 4096, NULL, 5, NULL);
    xTaskCreate(stat_task, "foreva_stat", 4096, NULL, 4, NULL);
    xTaskCreate(command_task, "foreva_command", 3072, NULL, 3, NULL);
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
}
