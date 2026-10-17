#include "lock_ai_adapter.h"
#include "ai_agent.h"
#include "app_config.h"
#include "app_diagnostics.h"
#include "ble_phone.h"
#include "event_log.h"
#include "remote_log.h"
#include "led.h"
#include "lock_control.h"
#include "ulp_controller.h"
#include "wifi_ota.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "lock_ai";
static adc_cali_handle_t s_battery_adc_cali;
static bool s_battery_adc_cali_attempted;
#define BATTERY_DIVIDER_HIGH_KOHM 150
#define BATTERY_DIVIDER_LOW_KOHM 27

int lock_ai_battery_mv_from_raw(int raw)
{
    if (raw < 0) return -1;
    if (!s_battery_adc_cali_attempted) {
        s_battery_adc_cali_attempted = true;
        const adc_cali_curve_fitting_config_t config = {
            .unit_id = ADC_UNIT_1,
            .chan = ADC_CHANNEL_4,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        esp_err_t err = adc_cali_create_scheme_curve_fitting(&config,
                                                              &s_battery_adc_cali);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "GPIO5 ADC calibration unavailable: %s", esp_err_to_name(err));
        }
    }
    if (!s_battery_adc_cali) return -1;

    int adc_mv = 0;
    if (adc_cali_raw_to_voltage(s_battery_adc_cali, raw, &adc_mv) != ESP_OK) return -1;
    return (adc_mv * (BATTERY_DIVIDER_HIGH_KOHM + BATTERY_DIVIDER_LOW_KOHM) +
            BATTERY_DIVIDER_LOW_KOHM / 2) / BATTERY_DIVIDER_LOW_KOHM;
}

esp_err_t lock_ai_status_json(char *json, size_t capacity)
{
    ble_phone_status_t status;
    ble_phone_get_status(&status);
    lock_feedback_t lock;
    lock_control_get_feedback(&lock);
    if (!json || capacity < 704) return ESP_ERR_INVALID_SIZE;
    bool known = lock.valid && !lock.fault_flags &&
        (lock.state == LOCK_STATE_LOCKED || lock.state == LOCK_STATE_UNLOCKED);
    nvs_stats_t nvs_stats;
    int free_entries = nvs_get_stats(NULL, &nvs_stats) == ESP_OK ? (int)nvs_stats.free_entries : -1;
    char rssi[16];
    if (status.rssi_valid) snprintf(rssi, sizeof(rssi), "%d", status.rssi);
    else strcpy(rssi, "null");
    int n = snprintf(json, capacity,
             "{\"version\":\"%s\",\"build\":\"%s\",\"open\":%s,\"hold_open\":%s,"
             "\"state\":\"%s\",\"locked\":%s,\"fault_flags\":%lu,"
             "\"requested_open\":%s,\"test_open_active\":%s,\"close_rejected\":%s,\"connected\":%s,"
             "\"encrypted\":%s,\"bonded\":%s,\"rssi\":%s,\"uptime_s\":%lld,\"stack_lock_bytes\":%u,\"stack_ble_bytes\":%u,\"stack_http_bytes\":%u,\"nvs_free_entries\":%d,\"calibration_required\":%s,\"rom_download_disabled\":%s}",
             APP_VERSION, APP_BUILD_TIMESTAMP,
             !known ? "null" : lock.state == LOCK_STATE_UNLOCKED ? "true" : "false",
             lock.hold_open ? "true" : "false", lock_control_feedback_name(&lock),
             known && lock.state == LOCK_STATE_LOCKED ? "true" : "false",
             (unsigned long)lock.fault_flags, lock.requested_open ? "true" : "false",
             lock_control_web_test_active() ? "true" : "false",
             lock.close_rejected ? "true" : "false", status.connected ? "true" : "false",
             status.encrypted ? "true" : "false", status.bonded ? "true" : "false",
             rssi, (long long)(esp_timer_get_time() / 1000000),
             lock_control_stack_free(), ble_phone_stack_free(),
             (unsigned)uxTaskGetStackHighWaterMark(NULL), free_entries,
             lock.valid && lock.calibration_required ? "true" : "false",
             esp_efuse_read_field_bit(ESP_EFUSE_DIS_DOWNLOAD_MODE) ? "true" : "false");
    return n >= 0 && (size_t)n < capacity ? ESP_OK : ESP_ERR_INVALID_SIZE;
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
        case LOCK_FSM_OPEN_HOLD: return "Постоянно открыт: без измерения";
        case LOCK_FSM_GATE_TEST: return "Диагностика GPIO6: MOSFET включён";
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

esp_err_t lock_ai_ulp_json(char *json, size_t capacity)
{
    if (!json || capacity < 4096) return ESP_ERR_INVALID_SIZE;
    ulp_diagnostics_t d;
    /* A complete ULP close/calibration cycle can hold the snapshot for 400 ms. */
    bool ready = false;
    for (int attempt = 0; attempt < 60; ++attempt) {
        if (ulp_controller_read_diagnostics(&d)) { ready = true; break; }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!ready) return ESP_ERR_INVALID_STATE;
    const int32_t verify_offset =
        d.calibration_verify_contact_raw - d.calibration_contact_raw;
    int32_t verify_adjusted = d.close_verify_delta_raw - verify_offset;
    if (verify_adjusted < 0) verify_adjusted = 0;
    int32_t contact_threshold_raw = -1;
    int32_t verify_contact_threshold_raw = -1;
    int32_t verify_contact_min_raw = -1;
    if (d.calibration_contact_raw >= 0 &&
        d.calibration_paper1_raw > d.calibration_contact_raw) {
        contact_threshold_raw = d.calibration_contact_raw +
            (d.calibration_paper1_raw - d.calibration_contact_raw) *
            (int32_t)ULP_CONTACT_BOUNDARY_PERCENT / 100;
        if (d.calibration_verify_contact_raw >= 0) {
            int32_t verify_tolerance =
                (d.calibration_paper1_raw - d.calibration_contact_raw) *
                (int32_t)ULP_CONTACT_BOUNDARY_PERCENT / 100;
            verify_contact_min_raw = 0;
            verify_contact_threshold_raw = d.calibration_verify_contact_raw + verify_tolerance;
        }
    }
    const int battery_mv = lock_ai_battery_mv_from_raw(d.vbat_raw);
    int length = snprintf(json, capacity,
             "{\"lock_state\":%u,\"lock_state_name\":\"%s\","
             "\"fsm_state\":%u,\"fsm_state_name\":\"%s\","
             "\"plate_state\":%u,\"plate_state_name\":\"%s\","
             "\"fault_flags\":%lu,\"event_flags\":%lu,\"fault_retry_count\":%lu,"
             "\"calibration_mode\":%lu,\"calibration_timeout_count\":%lu,\"calibration_valid\":%s,"
             "\"baseline_raw\":%ld,\"vbat_raw\":%ld,\"battery_mv\":%d,"
             "\"sense_raw\":%ld,\"delta_raw\":%ld,"
             "\"normalized_feature\":%lu,\"coil_current_raw\":%lu,"
             "\"measurement_counter\":%lu,\"pulse_actual_us\":%lu,"
             "\"close_verify_state\":%lu,\"close_verify_feature\":%lu,"
             "\"close_detected_state\":%lu,"
             "\"close_verify_delta_raw\":%ld,"
             "\"close_verify_adjusted_raw\":%ld,\"verify_offset_raw\":%ld,"
             "\"close_attempt_counter\":%lu,"
             "\"close_reject_latched\":%lu,"
             "\"calibration_verify_counter\":%lu,\"calibration_verify_raw\":%ld,"
             "\"calibration_contact_raw\":%ld,\"calibration_paper1_raw\":%ld,"
             "\"calibration_paper2_raw\":%ld,\"calibration_open_raw\":%ld,"
             "\"calibration_verify_contact_raw\":%ld,"
             "\"contact_boundary_percent\":%u,\"contact_threshold_raw\":%ld,"
             "\"verify_contact_boundary_percent\":%u,"
             "\"verify_contact_min_raw\":%ld,"
             "\"verify_contact_threshold_raw\":%ld,"
             "\"feature_contact_max\":%u,\"feature_gap1_max\":%u,"
             "\"feature_gap2_max\":%u,\"feature_gap3_max\":%u,"
             "\"feature_gap4_max\":%u,\"feature_gap5_max\":%u,"
             "\"command_sequence\":%lu,\"command_ack_sequence\":%lu,"
             "\"command_overlap_counter\":%lu,\"event_sequence\":%lu}",
             (unsigned)d.lock_state, ulp_lock_state_name(d.lock_state),
             (unsigned)d.fsm_state, ulp_fsm_state_name(d.fsm_state),
             (unsigned)d.plate_state, ulp_plate_state_name(d.plate_state),
             (unsigned long)d.fault_flags, (unsigned long)d.event_flags,
             (unsigned long)d.fault_retry_count, (unsigned long)d.calibration_mode,
             (unsigned long)d.calibration_timeout_count, d.calibration_valid ? "true" : "false",
             (long)d.baseline_raw, (long)d.vbat_raw, battery_mv, (long)d.sense_raw,
             (long)d.delta_raw, (unsigned long)d.normalized_feature,
             (unsigned long)d.coil_current_raw, (unsigned long)d.measurement_counter,
             (unsigned long)d.pulse_actual_us, (unsigned long)d.close_verify_state,
             (unsigned long)d.close_verify_feature,
             (unsigned long)d.close_detected_state,
             (long)d.close_verify_delta_raw,
             (long)verify_adjusted, (long)verify_offset,
             (unsigned long)d.close_attempt_counter,
             (unsigned long)d.close_reject_latched,
             (unsigned long)d.calibration_verify_counter,
             (long)d.calibration_verify_raw,
             (long)d.calibration_contact_raw, (long)d.calibration_paper1_raw,
             (long)d.calibration_paper2_raw, (long)d.calibration_open_raw,
             (long)d.calibration_verify_contact_raw,
             ULP_CONTACT_BOUNDARY_PERCENT, (long)contact_threshold_raw,
             ULP_CONTACT_BOUNDARY_PERCENT,
             (long)verify_contact_min_raw,
             (long)verify_contact_threshold_raw,
             ULP_FEATURE_CONTACT_MAX, ULP_FEATURE_GAP1_MAX,
             ULP_FEATURE_GAP2_MAX, ULP_FEATURE_GAP3_MAX,
             ULP_FEATURE_GAP4_MAX, ULP_FEATURE_GAP5_MAX,
             (unsigned long)d.command_sequence,
             (unsigned long)d.command_ack_sequence,
             (unsigned long)d.command_overlap_counter,
             (unsigned long)d.event_sequence);
    return length >= 0 && (size_t)length < capacity ? ESP_OK : ESP_ERR_INVALID_SIZE;
}


static esp_err_t status_provider(char *out, size_t capacity, void *ctx)
{
    (void)ctx;
    esp_err_t err = lock_ai_status_json(out, capacity);
    if (err != ESP_OK) return err;
    size_t used = strlen(out);
    if (!used || out[used - 1] != '}' || capacity - used < 16) return ESP_ERR_INVALID_SIZE;
    /* Preserve the existing flat status and include the full ULP diagnostics. */
    memcpy(out + used - 1, ",\"ulp\":", 7);
    used += 6;
    err = lock_ai_ulp_json(out + used, capacity - used - 1);
    if (err == ESP_ERR_INVALID_STATE) {
        snprintf(out + used, capacity - used, "null,\"ulp_error\":\"snapshot unavailable\"}");
        return ESP_OK;
    }
    if (err != ESP_OK) return err;
    used = strlen(out);
    out[used++] = '}';
    out[used] = 0;
    return ESP_OK;
}
static esp_err_t events_provider(char *out, size_t capacity, void *ctx)
{
    (void)ctx;
    if (capacity < 8192) return ESP_ERR_INVALID_SIZE;
    event_log_to_json(out, capacity);
    return ESP_OK;
}
static size_t monitor_provider(uint64_t since, char *out, size_t capacity,
    uint64_t *next, uint64_t *oldest, bool *dropped, void *ctx)
{
    (void)ctx;
    return remote_log_read(since, out, capacity, next, oldest, dropped);
}
static size_t pulses_provider(uint64_t since, char *out, size_t capacity,
    uint64_t *next, uint64_t *oldest, bool *dropped, void *ctx)
{
    (void)ctx;
    return ulp_controller_read_pulse_records(since, out, capacity, next, oldest, dropped);
}
static void prepare_restart(void *ctx)
{
    (void)ctx;
    event_log_add("Web: команда перезагрузки принята");
    wifi_ota_prepare_restart();
}
static void ota_complete(void *ctx)
{
    (void)ctx;
    led_set_state(LED_STATE_OTA);
}
#define SIMPLE_COMMAND(name, call) \
    static esp_err_t name(const ai_agent_args_t *args, void *ctx) \
    { (void)args; (void)ctx; return (call); }
SIMPLE_COMMAND(cmd_open, lock_request_manual_open())
SIMPLE_COMMAND(cmd_close, lock_request_close())
SIMPLE_COMMAND(cmd_auto, lock_request_set_hold_open(false))
SIMPLE_COMMAND(cmd_always_open, lock_request_set_hold_open(true))
SIMPLE_COMMAND(cmd_toggle, lock_request_toggle_hold_open())
SIMPLE_COMMAND(cmd_arm, lock_request_arm_open())
SIMPLE_COMMAND(cmd_disable, lock_request_disable_auto_and_close())
static esp_err_t cmd_ulp_lock(const ai_agent_args_t *args, void *ctx)
{
    (void)args; (void)ctx;
    return ulp_controller_request_wait(ULP_CMD_LOCK);
}
static esp_err_t cmd_ulp_unlock(const ai_agent_args_t *args, void *ctx)
{
    (void)args; (void)ctx;
    return ulp_controller_request_wait(ULP_CMD_UNLOCK);
}
static esp_err_t cmd_ulp_gate_test(const ai_agent_args_t *args, void *ctx)
{
    (void)args; (void)ctx;
    return ulp_controller_request_wait(ULP_CMD_GATE_TEST);
}
static esp_err_t cmd_ulp_gate_test_stop(const ai_agent_args_t *args, void *ctx)
{
    (void)args; (void)ctx;
    return ulp_controller_request_wait(ULP_CMD_HOLD_OPEN);
}
static esp_err_t cmd_buzzer(const ai_agent_args_t *args, void *ctx)
{
    (void)ctx;
    const char *value = ai_agent_arg(args, "value");
    if (!value) return ESP_ERR_INVALID_ARG;
    char *end;
    unsigned long pattern = strtoul(value, &end, 10);
    if (!*value || *end || pattern < 1 || pattern > 7) return ESP_ERR_INVALID_ARG;
    return app_buzzer_request_test((uint32_t)pattern);
}
static esp_err_t cmd_diagnostics(const ai_agent_args_t *args, void *ctx)
{
    (void)ctx;
    const char *value = ai_agent_arg(args, "value");
    if (!value) return ESP_ERR_INVALID_ARG;
    app_ulp_diagnostics_set_verbose(!strcmp(value, "1") || !strcmp(value, "true"));
    return ESP_OK;
}
static const ai_agent_command_t lock_commands[] = {
    {"open", "Open lock", cmd_open, NULL},
    {"close", "Close lock", cmd_close, NULL},
    {"auto", "Enable automatic mode", cmd_auto, NULL},
    {"always_open", "Hold lock open", cmd_always_open, NULL},
    {"toggle_hold_open", "Toggle hold-open mode", cmd_toggle, NULL},
    {"arm_open", "Arm opening", cmd_arm, NULL},
    {"disable_auto_close", "Disable automatic mode and close", cmd_disable, NULL},
    {"ulp_lock", "Direct ULP lock request", cmd_ulp_lock, NULL},
    {"ulp_unlock", "Direct ULP unlock request", cmd_ulp_unlock, NULL},
    {"ulp_gate_test", "Timed GPIO6 ADC test with MOSFET on", cmd_ulp_gate_test, NULL},
    {"ulp_gate_test_stop", "Stop GPIO6 ADC test and turn MOSFET off", cmd_ulp_gate_test_stop, NULL},
    {"buzzer", "Test buzzer: value=1..7", cmd_buzzer, NULL},
    {"diagnostics", "Verbose ULP logging: value=0 or 1", cmd_diagnostics, NULL},
};
static esp_err_t execute_logged(const ai_agent_args_t *args, void *ctx)
{
    const ai_agent_command_t *cmd = ctx;
    esp_err_t err = cmd->execute(args, cmd->ctx);
    if (err == ESP_OK) event_log_add("Agent API: команда %s принята", cmd->name);
    return err;
}
esp_err_t lock_ai_adapter_init(void)
{
    static bool initialized;
    static bool core_initialized;
    static size_t registered;
    static char device_id[18];
    static const char *const legacy[] = {"status", "ulp_diagnostics", "event_log",
        "lock", "unlock", "auto", "always_open", "direct_ulp", "buzzer_test",
        "remote_monitor", "ulp_diagnostics_mode", "restart", "ota", NULL};
    if (initialized) return ESP_OK;
    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) return err;
    snprintf(device_id, sizeof(device_id), "%02x:%02x:%02x:%02x:%02x:%02x",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    const ai_agent_config_t cfg = {
        .device_type = "ble_lock_esp32s3", .device_name = "TheKidLock",
        .firmware_version = APP_VERSION, .device_id = device_id,
        .build = APP_BUILD_TIMESTAMP, .legacy_capabilities = legacy,
        .status = status_provider, .events = events_provider, .monitor = monitor_provider,
        .pulses = pulses_provider,
        .prepare_restart = prepare_restart, .ota_complete = ota_complete,
    };
    if (!core_initialized) {
        err = ai_agent_init(&cfg);
        if (err != ESP_OK) return err;
        core_initialized = true;
    }
    for (size_t i = registered; i < sizeof(lock_commands) / sizeof(lock_commands[0]); i++) {
        ai_agent_command_t cmd = lock_commands[i];
        cmd.execute = execute_logged;
        cmd.ctx = (void *)&lock_commands[i];
        err = ai_agent_register_command(&cmd);
        if (err != ESP_OK) return err;
        registered = i + 1;
    }
    err = ai_agent_seal();
    if (err != ESP_OK) return err;
    initialized = true;
    return ESP_OK;
}
