#include "ulp_controller.h"

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_private/adc_share_hw_ctrl.h"
#include "hal/adc_ll.h"
#include "ulp_main.h"
#include "ulp_riscv.h"

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[] asm("_binary_ulp_main_bin_end");

static bool s_initialized;

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
    err = adc_oneshot_config_channel(adc, ADC_SENSE_CHANNEL, &cfg);
    if (err != ESP_OK) return err;
    err = adc_oneshot_config_channel(adc, ADC_VBAT_CHANNEL, &cfg);
    if (err != ESP_OK) return err;

    adc_oneshot_ll_set_atten(ADC_UNIT_1, ADC_SENSE_CHANNEL, ADC_ATTEN_DB_12);
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
    ulp_lock_state = LOCK_STATE_STARTING;
    ulp_event_sequence = 0;
    ulp_event_flags = ULP_EVENT_NONE;
    ulp_fsm_state = LOCK_FSM_UNLOCKED_SCAN;
    ulp_coil_current_raw = 0;
    ulp_close_verify_state = PLATE_UNKNOWN;
    ulp_close_verify_feature = 0;
    ulp_close_verify_delta_raw = -1;
    ulp_close_attempt_counter = 0;

    err = ulp_set_wakeup_period(0, ULP_MEASURE_PERIOD_US);
    if (err != ESP_OK) return err;
    err = ulp_riscv_run();
    if (err != ESP_OK) return err;
    s_initialized = true;
    return ESP_OK;
}

void ulp_controller_request(ulp_command_t requested)
{
    if (!s_initialized) return;
    uint32_t sequence = ulp_command_sequence;
    if (sequence & 1u) sequence++;
    ulp_command_sequence = sequence + 1u;
    ulp_command = (uint32_t)requested;
    ulp_command_sequence = sequence + 2u;
}

bool ulp_controller_read_state(lock_state_t *state, uint32_t *faults,
                               uint32_t *events)
{
    if (!s_initialized || !state || !faults || !events) return false;
    for (int tries = 0; tries < 4; tries++) {
        uint32_t before = ulp_sequence;
        if (before & 1u) continue;
        lock_state_t current_state = (lock_state_t)ulp_lock_state;
        uint32_t current_faults = ulp_fault_flags;
        uint32_t current_events = ulp_event_flags;
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
    for (int tries = 0; tries < 4; tries++) {
        uint32_t before = ulp_sequence;
        if (before & 1u) continue;
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
            .measurement_counter = ulp_measurement_counter,
            .pulse_actual_us = ulp_pulse_actual_us,
            .close_verify_state = ulp_close_verify_state,
            .close_verify_feature = ulp_close_verify_feature,
            .close_verify_delta_raw = ulp_close_verify_delta_raw,
            .close_attempt_counter = ulp_close_attempt_counter,
            .command_ack_sequence = ulp_command_ack_sequence,
            .event_sequence = ulp_event_sequence,
        };
        uint32_t after = ulp_sequence;
        if (before == after && !(after & 1u)) {
            *out = snapshot;
            return true;
        }
    }
    return false;
}
