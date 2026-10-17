#include "ulp_controller.h"
#include "lock_control.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_private/adc_share_hw_ctrl.h"
#include "hal/adc_ll.h"
#include "ulp_main.h"
#include "ulp_riscv.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "event_log.h"
#include "app_config.h"

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[] asm("_binary_ulp_main_bin_end");

static bool s_initialized;
static volatile uint32_t s_command_overlap_counter;
static portMUX_TYPE s_command_lock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t s_calibration_mutex;
static esp_timer_handle_t s_calibration_timer;
static int64_t s_calibration_deadline_us;
static uint32_t s_calibration_timeout_count;
static uint8_t s_sense_gpio = APP_DEFAULT_SHUNT_GPIO;
static adc_channel_t s_sense_adc_channel = ADC_SENSE_CHANNEL_DEFAULT;
#define ULP_PULSE_RING_CAPACITY 256
static ulp_pulse_record_t s_pulse_ring[ULP_PULSE_RING_CAPACITY];
static uint64_t s_pulse_written;
static uint32_t s_last_pulse_sequence;
static uint32_t s_source_pulse_dropped;
static portMUX_TYPE s_pulse_lock = portMUX_INITIALIZER_UNLOCKED;

static bool shunt_gpio_to_adc_channel(uint8_t gpio, adc_channel_t *channel)
{
    if (!channel) return false;
    switch (gpio) {
        case 1: *channel = ADC_CHANNEL_0; return true;
        case 2: *channel = ADC_CHANNEL_1; return true;
        case 4: *channel = ADC_CHANNEL_3; return true;
        case 6: *channel = ADC_CHANNEL_5; return true;
        case 7: *channel = ADC_CHANNEL_6; return true;
        case 10: *channel = ADC_CHANNEL_9; return true;
        default: return false;
    }
}

/* Caller owns calibration mutex. This does not write the command mailbox. */
static void calibration_end(void)
{
    ulp_calibration_verify_request = 0;
    ulp_close_reject_latched = 0;
    ulp_calibration_mode = 0;
    s_calibration_deadline_us = 0;
}

static bool calibration_expire(void)
{
    if (ulp_calibration_mode && esp_timer_get_time() >= s_calibration_deadline_us) {
        calibration_end();
        s_calibration_timeout_count++;
        event_log_add("ULP: калибровка отменена по таймауту 120 с");
        return true;
    }
    return false;
}

static void calibration_timeout(void *arg)
{
    (void)arg;
    /* Never block the shared esp_timer task behind an NVS save. */
    if (xSemaphoreTake(s_calibration_mutex, 0) != pdTRUE) return;
    calibration_expire();
    xSemaphoreGive(s_calibration_mutex);
}

#define ULP_CAL_NS "ulp_cal"

static void load_calibration(void)
{
    nvs_handle_t nvs = 0;
    if (nvs_open(ULP_CAL_NS, NVS_READONLY, &nvs) != ESP_OK) return;
    int32_t contact = -1, paper1 = -1, paper2 = -1, open = -1, verify = -1;
    uint8_t calibration_gpio = APP_DEFAULT_SHUNT_GPIO;
    const esp_err_t gpio_err = nvs_get_u8(nvs, "gpio", &calibration_gpio);
    const bool calibration_matches_gpio = gpio_err == ESP_OK
        ? calibration_gpio == s_sense_gpio
        : s_sense_gpio == APP_DEFAULT_SHUNT_GPIO;
    if (calibration_matches_gpio &&
        nvs_get_i32(nvs, "contact", &contact) == ESP_OK &&
        nvs_get_i32(nvs, "paper1", &paper1) == ESP_OK &&
        nvs_get_i32(nvs, "paper2", &paper2) == ESP_OK &&
        nvs_get_i32(nvs, "open", &open) == ESP_OK &&
        nvs_get_i32(nvs, "verify", &verify) == ESP_OK &&
        ulp_calibration_values_valid(contact, paper1, paper2, open, verify)) {
        ulp_calibration_contact_raw = contact;
        ulp_calibration_paper1_raw = paper1;
        ulp_calibration_paper2_raw = paper2;
        ulp_calibration_open_raw = open;
        ulp_calibration_verify_contact_raw = verify;
    }
    nvs_close(nvs);
}

static esp_err_t init_gate(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << LOCK_GATE_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) return err;
    return gpio_set_level(LOCK_GATE_GPIO, 0);
}

static esp_err_t init_adc(void)
{
    app_config_t app_cfg;
    app_config_get(&app_cfg);
    adc_channel_t sense_channel;
    if (!shunt_gpio_to_adc_channel(app_cfg.shunt_gpio, &sense_channel)) {
        app_cfg.shunt_gpio = APP_DEFAULT_SHUNT_GPIO;
        if (!shunt_gpio_to_adc_channel(app_cfg.shunt_gpio, &sense_channel)) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    s_sense_gpio = app_cfg.shunt_gpio;
    s_sense_adc_channel = sense_channel;

    adc_oneshot_unit_handle_t adc;
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_RISCV,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &adc);
    if (err != ESP_OK) return err;

    adc_oneshot_chan_cfg_t cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(adc, s_sense_adc_channel, &cfg);
    if (err != ESP_OK) return err;
    err = adc_oneshot_config_channel(adc, ADC_VBAT_CHANNEL, &cfg);
    if (err != ESP_OK) return err;

    adc_oneshot_ll_set_atten(ADC_UNIT_1, s_sense_adc_channel, ADC_ATTEN_DB_12);
    adc_oneshot_ll_set_atten(ADC_UNIT_1, ADC_VBAT_CHANNEL, ADC_ATTEN_DB_12);
    adc_set_hw_calibration_code(ADC_UNIT_1, ADC_ATTEN_DB_12);
    return ESP_OK;
}

esp_err_t ulp_controller_init(void)
{
    if (s_initialized) return ESP_OK;

    ulp_riscv_timer_stop();
    ulp_riscv_halt();
    ulp_riscv_reset();

    esp_err_t err = init_gate();
    if (err != ESP_OK) return err;
    err = init_adc();
    if (err != ESP_OK) return err;

    err = ulp_riscv_load_binary(ulp_main_bin_start,
                                ulp_main_bin_end - ulp_main_bin_start);
    if (err != ESP_OK) return err;

    ulp_sense_adc_channel = s_sense_adc_channel;

    ulp_sequence = 0;
    ulp_baseline_raw = -1;
    ulp_vbat_raw = -1;
    ulp_sense_raw = -1;
    ulp_delta_raw = -1;
    ulp_normalized_feature = 0;
    ulp_plate_state = PLATE_UNKNOWN;
    ulp_fault_flags = 0;
    ulp_measurement_counter = 0;
    ulp_pulse_actual_us = 0;
    ulp_command_sequence = 0;
    ulp_command = ULP_CMD_UNLOCK;
    ulp_command_ack_sequence = 0;
    s_command_overlap_counter = 0;
    ulp_lock_state = LOCK_STATE_STARTING;
    ulp_event_sequence = 0;
    ulp_event_flags = ULP_EVENT_NONE;
    ulp_fsm_state = LOCK_FSM_UNLOCKED_SCAN;
    ulp_coil_current_raw = 0;
    ulp_gate_level = 0;
    ulp_gate_high_counter = 0;
    ulp_gate_low_counter = 0;
    ulp_close_verify_state = PLATE_UNKNOWN;
    ulp_close_detected_state = PLATE_UNKNOWN;
    ulp_close_verify_feature = 0;
    ulp_close_verify_delta_raw = -1;
    ulp_close_attempt_counter = 0;
    ulp_close_reject_counter = 0;
    ulp_close_reject_latched = 0;
    ulp_calibration_mode = 0;
    ulp_fault_retry_count = 0;
    ulp_calibration_contact_raw = -1;
    ulp_calibration_paper1_raw = -1;
    ulp_calibration_paper2_raw = -1;
    ulp_calibration_open_raw = -1;
    ulp_calibration_verify_contact_raw = -1;
    ulp_calibration_verify_request = 0;
    ulp_calibration_verify_counter = 0;
    ulp_calibration_verify_raw = -1;
    load_calibration();

    s_calibration_mutex = xSemaphoreCreateMutex();
    if (!s_calibration_mutex) return ESP_ERR_NO_MEM;
    const esp_timer_create_args_t timeout_args = {
        .callback = calibration_timeout, .name = "ulp_cal_timeout",
    };
    err = esp_timer_create(&timeout_args, &s_calibration_timer);
    if (err != ESP_OK) return err;
    err = esp_timer_start_periodic(s_calibration_timer, 100000);
    if (err != ESP_OK) return err;

    err = ulp_set_wakeup_period(0, ULP_MEASURE_PERIOD_US);
    if (err != ESP_OK) return err;
    err = ulp_riscv_run();
    if (err != ESP_OK) return err;
    s_initialized = true;
    return ESP_OK;
}

esp_err_t ulp_controller_calibration_start(void)
{
    if(lock_control_internet_active()) return ESP_ERR_INVALID_STATE;
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_calibration_mutex, portMAX_DELAY);
    calibration_expire();
    if (ulp_calibration_mode) {
        xSemaphoreGive(s_calibration_mutex);
        event_log_add("CAL start rejected: already active");
        return ESP_ERR_INVALID_STATE;
    }
    s_calibration_deadline_us = esp_timer_get_time() + ULP_CALIBRATION_TIMEOUT_US;
    ulp_close_reject_latched = 0;
    ulp_calibration_mode = 1;
    xSemaphoreGive(s_calibration_mutex);
    event_log_add("ULP: калибровка включена на 120 с, замок отпущен");
    return ESP_OK;
}

void ulp_controller_calibration_cancel(void)
{
    if (!s_initialized) return;
    xSemaphoreTake(s_calibration_mutex, portMAX_DELAY);
    const bool was_active = ulp_calibration_mode != 0;
    calibration_end();
    xSemaphoreGive(s_calibration_mutex);
    event_log_add("CAL session ended active=%u", (unsigned)was_active);
}

esp_err_t ulp_controller_calibration_verify(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_calibration_mutex, portMAX_DELAY);
    calibration_expire();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (ulp_calibration_mode && !ulp_calibration_verify_request) {
        ulp_calibration_verify_request = 1;
        err = ESP_OK;
    }
    const unsigned active = (unsigned)ulp_calibration_mode;
    const unsigned busy = (unsigned)ulp_calibration_verify_request;
    xSemaphoreGive(s_calibration_mutex);
    event_log_add("CAL verify request result=%s active=%u busy=%u",
                  esp_err_to_name(err), active, busy);
    return err;
}

esp_err_t ulp_controller_calibration_save(int32_t closed_raw,
                                          int32_t open_raw,
                                          int32_t verify_contact_raw)
{
    const int64_t span = (int64_t)open_raw - closed_raw;
    /* Estimate one-sheet offset as 1/6.14 of the closed-to-open ADC span. */
    const int64_t paper1_calculated = closed_raw + span * 100 / 614;
    const int64_t paper2_calculated =
        paper1_calculated + (paper1_calculated - closed_raw);
    if (!s_initialized || closed_raw < 0 || closed_raw > 4095 ||
        open_raw <= closed_raw || open_raw > 4095 ||
        paper1_calculated <= closed_raw ||
        paper2_calculated >= open_raw || paper2_calculated > 4095 ||
        verify_contact_raw < 0 || verify_contact_raw > 4095) {
        event_log_add("CAL save rejected closed=%ld open=%ld verify=%ld",
                      (long)closed_raw, (long)open_raw,
                      (long)verify_contact_raw);
        return ESP_ERR_INVALID_ARG;
    }
    const int32_t contact_raw = closed_raw;
    const int32_t paper1_raw = (int32_t)paper1_calculated;
    const int32_t paper2_raw = (int32_t)paper2_calculated;
    xSemaphoreTake(s_calibration_mutex, portMAX_DELAY);
    calibration_expire();
    if (!ulp_calibration_mode) {
        xSemaphoreGive(s_calibration_mutex);
        event_log_add("CAL save rejected: session inactive");
        return ESP_ERR_INVALID_STATE;
    }
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(ULP_CAL_NS, NVS_READWRITE, &nvs);
    if (err == ESP_OK) err = nvs_set_i32(nvs, "contact", contact_raw);
    if (err == ESP_OK) err = nvs_set_i32(nvs, "paper1", paper1_raw);
    if (err == ESP_OK) err = nvs_set_i32(nvs, "paper2", paper2_raw);
    if (err == ESP_OK) err = nvs_set_i32(nvs, "open", open_raw);
    if (err == ESP_OK) err = nvs_set_i32(nvs, "verify", verify_contact_raw);
    if (err == ESP_OK) err = nvs_set_u8(nvs, "gpio", s_sense_gpio);
    if (err == ESP_OK) err = nvs_commit(nvs);
    if (nvs) nvs_close(nvs);
    if (err != ESP_OK) {
        xSemaphoreGive(s_calibration_mutex);
        event_log_add("CAL save failed: %s", esp_err_to_name(err));
        return err;
    }
    ulp_calibration_contact_raw = contact_raw;
    ulp_calibration_paper1_raw = paper1_raw;
    ulp_calibration_paper2_raw = paper2_raw;
    ulp_calibration_open_raw = open_raw;
    ulp_calibration_verify_contact_raw = verify_contact_raw;
    calibration_end();
    xSemaphoreGive(s_calibration_mutex);
    event_log_add("CAL saved closed=%ld paper1=%ld paper2=%ld open=%ld verify=%ld",
                  (long)contact_raw, (long)paper1_raw, (long)paper2_raw, (long)open_raw,
                  (long)verify_contact_raw);
    return ESP_OK;
}

static esp_err_t request_command(ulp_command_t requested, uint32_t *issued)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (requested != ULP_CMD_LOCK && requested != ULP_CMD_UNLOCK &&
        requested != ULP_CMD_HOLD_OPEN && requested != ULP_CMD_GATE_TEST)
        return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_command_lock);
    if (ulp_command_sequence != ulp_command_ack_sequence ||
        ulp_close_reject_latched) {
        s_command_overlap_counter++;
    }
    uint32_t sequence = ulp_command_sequence;
    if (sequence & 1u) sequence++;
    ulp_command_sequence = sequence + 1u;
    __sync_synchronize();
    ulp_command = (uint32_t)requested;
    __sync_synchronize();
    ulp_command_sequence = sequence + 2u;
    if (issued) *issued = sequence + 2u;
    portEXIT_CRITICAL(&s_command_lock);
    return ESP_OK;
}

esp_err_t ulp_controller_request(ulp_command_t requested)
{
    return request_command(requested, NULL);
}

esp_err_t ulp_controller_request_wait(ulp_command_t requested)
{
    if(lock_control_internet_active()) return ESP_ERR_INVALID_STATE;
    if (ulp_calibration_mode) return ESP_ERR_INVALID_STATE;
    if (requested == ULP_CMD_GATE_TEST &&
        (ulp_lock_state != LOCK_STATE_UNLOCKED ||
         ulp_fsm_state != LOCK_FSM_OPEN_HOLD || ulp_gate_level)) {
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t issued;
    esp_err_t err = request_command(requested, &issued);
    if (err != ESP_OK) return err;
    const int64_t deadline = esp_timer_get_time() + 750000;
    do {
        __sync_synchronize();
        if (ulp_command_sequence != issued) return ESP_ERR_INVALID_STATE;
        if (ulp_command_ack_sequence == issued) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(10));
    } while (esp_timer_get_time() < deadline);
    /* Timeout is not cancellation: the mailbox may be consumed later. */
    return ESP_ERR_TIMEOUT;
}

bool ulp_controller_read_state(lock_state_t *state, uint32_t *faults,
                               uint32_t *events)
{
    if (!s_initialized || !state || !faults || !events) return false;
    for (int tries = 0; tries < 4; tries++) {
        uint32_t before = ulp_sequence;
        if (before & 1u) continue;
        __sync_synchronize();
        lock_state_t current_state = (lock_state_t)ulp_lock_state;
        uint32_t current_faults = ulp_fault_flags;
        uint32_t current_events = ulp_event_flags;
        __sync_synchronize();
        uint32_t after = ulp_sequence;
        if (before == after && !(after & 1u)) {
            *state = current_state;
            *faults = current_faults;
            *events = current_events;
            return true;
        }
    }
    return false;
}

bool ulp_controller_read_diagnostics(ulp_diagnostics_t *out)
{
    if (!s_initialized || !out) return false;
    const int64_t deadline_us = esp_timer_get_time() + 50000;
    while (esp_timer_get_time() < deadline_us) {
        uint32_t before = ulp_sequence;
        if (before & 1u) {
            vTaskDelay(1);
            continue;
        }
        __sync_synchronize();
        ulp_diagnostics_t snapshot = {
            .lock_state = (lock_state_t)ulp_lock_state,
            .fsm_state = (lock_fsm_state_t)ulp_fsm_state,
            .plate_state = (plate_state_t)ulp_plate_state,
            .fault_flags = ulp_fault_flags,
            .event_flags = ulp_event_flags,
            .baseline_raw = ulp_baseline_raw,
            .vbat_raw = ulp_vbat_raw,
            .sense_raw = ulp_sense_raw,
            .delta_raw = ulp_delta_raw,
            .normalized_feature = ulp_normalized_feature,
            .coil_current_raw = ulp_coil_current_raw,
            .gate_level = ulp_gate_level,
            .gate_high_counter = ulp_gate_high_counter,
            .gate_low_counter = ulp_gate_low_counter,
            .requested_command = ulp_command,
            .calibration_mode = ulp_calibration_mode,
            .calibration_valid = ulp_calibration_values_valid(ulp_calibration_contact_raw,
                ulp_calibration_paper1_raw, ulp_calibration_paper2_raw,
                ulp_calibration_open_raw, ulp_calibration_verify_contact_raw),
            .calibration_timeout_count = s_calibration_timeout_count,
            .fault_retry_count = ulp_fault_retry_count,
            .calibration_verify_counter = ulp_calibration_verify_counter,
            .calibration_verify_raw = ulp_calibration_verify_raw,
            .calibration_contact_raw = ulp_calibration_contact_raw,
            .calibration_paper1_raw = ulp_calibration_paper1_raw,
            .calibration_paper2_raw = ulp_calibration_paper2_raw,
            .calibration_open_raw = ulp_calibration_open_raw,
            .calibration_verify_contact_raw = ulp_calibration_verify_contact_raw,
            .measurement_counter = ulp_measurement_counter,
            .pulse_actual_us = ulp_pulse_actual_us,
            .close_verify_state = ulp_close_verify_state,
            .close_detected_state = ulp_close_detected_state,
            .close_verify_feature = ulp_close_verify_feature,
            .close_verify_delta_raw = ulp_close_verify_delta_raw,
            .close_attempt_counter = ulp_close_attempt_counter,
            .close_reject_counter = ulp_close_reject_counter,
            .close_reject_latched = ulp_close_reject_latched,
            .command_ack_sequence = ulp_command_ack_sequence,
            .command_sequence = ulp_command_sequence,
            .command_overlap_counter = s_command_overlap_counter,
            .event_sequence = ulp_event_sequence,
        };
        __sync_synchronize();
        uint32_t after = ulp_sequence;
        if (before == after && !(after & 1u)) {
            *out = snapshot;
            return true;
        }
        vTaskDelay(1);
    }
    return false;
}

bool ulp_controller_capture_latest_pulse(void)
{
    if (!s_initialized) return false;
    const uint32_t before = ulp_sequence;
    if (before & 1U) return false;
    __sync_synchronize();
    const uint32_t measurement = ulp_measurement_counter;
    const uint32_t pulse_us = ulp_pulse_actual_us;
    const uint32_t pulse_sequence = ulp_pulse_sequence;
    ulp_diagnostics_t d = {
        .lock_state = (lock_state_t)ulp_lock_state,
        .fsm_state = (lock_fsm_state_t)ulp_fsm_state,
        .plate_state = (plate_state_t)ulp_plate_state,
        .fault_flags = ulp_fault_flags,
        .event_flags = ulp_event_flags,
        .baseline_raw = ulp_baseline_raw,
        .vbat_raw = ulp_vbat_raw,
        .sense_raw = ulp_sense_raw,
        .delta_raw = ulp_delta_raw,
        .normalized_feature = ulp_normalized_feature,
        .requested_command = ulp_command,
        .measurement_counter = measurement,
        .pulse_actual_us = pulse_us,
    };
    __sync_synchronize();
    const uint32_t after = ulp_sequence;
    if (before != after || (after & 1U) || !pulse_us || pulse_sequence == s_last_pulse_sequence)
        return false;
    const uint32_t missed = pulse_sequence - s_last_pulse_sequence - 1;
    portENTER_CRITICAL(&s_pulse_lock);
    s_source_pulse_dropped += missed;
    const uint64_t cursor = s_pulse_written++;
    s_pulse_ring[cursor % ULP_PULSE_RING_CAPACITY] = (ulp_pulse_record_t){
        .cursor = cursor,
        .pulse_sequence = pulse_sequence,
        .measurement_counter = measurement,
        .pulse_actual_us = pulse_us,
        .baseline_raw = d.baseline_raw,
        .vbat_raw = d.vbat_raw,
        .sense_raw = d.sense_raw,
        .delta_raw = d.delta_raw,
        .normalized_feature = d.normalized_feature,
        .plate_state = d.plate_state,
        .fault_flags = d.fault_flags,
        .lock_state = d.lock_state,
        .fsm_state = d.fsm_state,
        .event_flags = d.event_flags,
        .requested_command = d.requested_command,
        .source_dropped_before = s_source_pulse_dropped,
    };
    s_last_pulse_sequence = pulse_sequence;
    portEXIT_CRITICAL(&s_pulse_lock);
    return true;
}

size_t ulp_controller_read_pulse_records(uint64_t since, char *out, size_t capacity,
    uint64_t *next, uint64_t *oldest, bool *dropped)
{
    if (!out || !capacity) return 0;
    portENTER_CRITICAL(&s_pulse_lock);
    uint64_t cursor = s_pulse_written > ULP_PULSE_RING_CAPACITY
        ? s_pulse_written - ULP_PULSE_RING_CAPACITY : 0;
    const uint64_t total = s_pulse_written;
    bool lost = since < cursor || since > total;
    if (lost) since = cursor;
    portEXIT_CRITICAL(&s_pulse_lock);

    size_t used = 0;
    while (since < total && used < capacity) {
        ulp_pulse_record_t record;
        portENTER_CRITICAL(&s_pulse_lock);
        const uint64_t current_oldest = s_pulse_written > ULP_PULSE_RING_CAPACITY
            ? s_pulse_written - ULP_PULSE_RING_CAPACITY : 0;
        if (since < current_oldest) {
            lost = true;
            since = current_oldest;
        }
        record = s_pulse_ring[since % ULP_PULSE_RING_CAPACITY];
        portEXIT_CRITICAL(&s_pulse_lock);
        if (record.cursor != since) { lost = true; break; }
        int n = snprintf(out + used, capacity - used,
            "%lu,%lu,%lu,%ld,%ld,%ld,%ld,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
            (unsigned long)record.pulse_sequence,
            (unsigned long)record.measurement_counter,
            (unsigned long)record.pulse_actual_us,
            (long)record.baseline_raw, (long)record.vbat_raw,
            (long)record.sense_raw, (long)record.delta_raw,
            (unsigned long)record.normalized_feature,
            (unsigned long)record.plate_state, (unsigned long)record.fault_flags,
            (unsigned long)record.lock_state, (unsigned long)record.fsm_state,
            (unsigned long)record.event_flags, (unsigned long)record.requested_command,
            (unsigned long)record.source_dropped_before);
        if (n < 0 || (size_t)n >= capacity - used) break;
        used += (size_t)n;
        since++;
    }
    portENTER_CRITICAL(&s_pulse_lock);
    const uint64_t final_oldest = s_pulse_written > ULP_PULSE_RING_CAPACITY
        ? s_pulse_written - ULP_PULSE_RING_CAPACITY : 0;
    portEXIT_CRITICAL(&s_pulse_lock);
    if (next) *next = since;
    if (oldest) *oldest = final_oldest;
    if (dropped) *dropped = lost;
    return used;
}
