#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "hal/adc_ll.h"
#include "esp_private/adc_share_hw_ctrl.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/rmt.h"
#include "esp_rom_sys.h"
#include "ulp_riscv.h"
#include "shared_state.h"
#include "ulp_main.h"
#include "controller_api.h"

static const char *TAG = "ULP_DEBUG";

#define STATUS_LED_GPIO GPIO_NUM_48
#define STATUS_LED_RMT_CHANNEL RMT_CHANNEL_0
#define STATUS_LED_BRIGHTNESS 48

// Temporary integration test. Set to 0 when Bluetooth commands are connected.
#define ULP_TEST_CYCLE_ENABLE 0
#define ULP_TEST_CLOSE_PERIOD_MS 30000U
// Keep the controller locked for this diagnostic run. Re-enable the timed
// OPEN only after the coil-open fault path has been verified.
#define ULP_TEST_AUTO_OPEN_ENABLE 0
#define ULP_TEST_OPEN_DELAY_MS 5000U

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[] asm("_binary_ulp_main_bin_end");
static bool led_ready;
static uint8_t led_r, led_g, led_b;
static bool led_color_valid;
static volatile bool ulp_event_pending;
static bool alarm_active;
static bool alarm_phase;
static TickType_t alarm_deadline;
static TickType_t alarm_next_toggle;
static uint32_t handled_event_sequence;

static void ulp_event_isr(void *arg)
{
    (void)arg;
    ulp_event_pending = true;
}

void lock_controller_request(ulp_command_t requested)
{
    uint32_t seq = ulp_command_sequence;
    if (seq & 1u) seq++;
    ulp_command_sequence = seq + 1u;
    ulp_command = (uint32_t)requested;
    ulp_command_sequence = seq + 2u;
}

bool lock_controller_take_event(uint32_t *flags)
{
    if (!ulp_event_pending || flags == NULL) return false;
    *flags = ulp_event_flags;
    ulp_event_pending = false;
    return true;
}

#if ULP_TEST_CYCLE_ENABLE
static void run_test_command_cycle(void)
{
    static bool initialized;
    static bool close_pending;
    static TickType_t next_close;
    static TickType_t open_deadline;
    const TickType_t now = xTaskGetTickCount();

    if (!initialized) {
        initialized = true;
        next_close = now + pdMS_TO_TICKS(ULP_TEST_CLOSE_PERIOD_MS);
    }

    if (!close_pending && (int32_t)(now - next_close) >= 0) {
        lock_controller_request(ULP_CMD_CLOSE);
        close_pending = true;
        open_deadline = now + pdMS_TO_TICKS(ULP_TEST_OPEN_DELAY_MS);
    }

    if (ULP_TEST_AUTO_OPEN_ENABLE && close_pending &&
        (int32_t)(now - open_deadline) >= 0) {
        lock_controller_request(ULP_CMD_OPEN);
        close_pending = false;
        next_close += pdMS_TO_TICKS(ULP_TEST_CLOSE_PERIOD_MS);
    }
}
#endif

static void led_init(void)
{
    rmt_config_t cfg = RMT_DEFAULT_CONFIG_TX(STATUS_LED_GPIO, STATUS_LED_RMT_CHANNEL);
    cfg.clk_div = 2;
    cfg.tx_config.loop_en = false;
    cfg.tx_config.carrier_en = false;
    cfg.tx_config.idle_output_en = true;
    cfg.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
    ESP_ERROR_CHECK(rmt_config(&cfg));
    ESP_ERROR_CHECK(rmt_driver_install(STATUS_LED_RMT_CHANNEL, 0, 0));
    led_ready = true;
}

static void led_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!led_ready || (led_color_valid && led_r == r && led_g == g && led_b == b)) return;
    const uint8_t grb[3] = {g, r, b};
    rmt_item32_t items[24];
    size_t n = 0;
    for (size_t k = 0; k < 3; k++) {
        for (int bit = 7; bit >= 0; bit--) {
            const bool one = (grb[k] >> bit) & 1u;
            items[n].level0 = 1;
            items[n].duration0 = one ? 28 : 14;
            items[n].level1 = 0;
            items[n].duration1 = one ? 24 : 32;
            n++;
        }
    }
    esp_err_t err = rmt_write_items(STATUS_LED_RMT_CHANNEL, items, n, false);
    if (err == ESP_OK) err = rmt_wait_tx_done(STATUS_LED_RMT_CHANNEL, pdMS_TO_TICKS(10));
    if (err != ESP_OK) {
        return;
    }
    esp_rom_delay_us(80);
    led_r = r; led_g = g; led_b = b; led_color_valid = true;
}

static void led_state(uint32_t state)
{
    switch ((plate_state_t)state) {
    case PLATE_CONTACT: led_rgb(0, STATUS_LED_BRIGHTNESS, 0); break;
    case PLATE_GAP_1: led_rgb(0, 0, STATUS_LED_BRIGHTNESS); break;
    case PLATE_GAP_2: led_rgb(STATUS_LED_BRIGHTNESS, 0, 0); break;
    case PLATE_GAP_3: led_rgb(STATUS_LED_BRIGHTNESS, (STATUS_LED_BRIGHTNESS * 3) / 4, 0); break;
    case PLATE_GAP_4: led_rgb(STATUS_LED_BRIGHTNESS, STATUS_LED_BRIGHTNESS, STATUS_LED_BRIGHTNESS); break;
    case PLATE_GAP_5: led_rgb(STATUS_LED_BRIGHTNESS, 0, STATUS_LED_BRIGHTNESS); break;
    default: led_rgb(0, 0, 0); break;
    }
}

static void start_fault_alarm(void)
{
    const TickType_t now = xTaskGetTickCount();
    alarm_active = true;
    alarm_phase = true;
    alarm_deadline = now + pdMS_TO_TICKS(3000U);
    alarm_next_toggle = now;
    led_rgb(STATUS_LED_BRIGHTNESS, 0, 0);
}

static void service_fault_alarm(void)
{
    if (!alarm_active) return;
    const TickType_t now = xTaskGetTickCount();
    if ((int32_t)(now - alarm_deadline) >= 0) {
        alarm_active = false;
        led_rgb(0, 0, 0);
        // Re-arm the ULP in OPEN mode after the three-second alarm window.
        lock_controller_request(ULP_CMD_OPEN);
        return;
    }
    if ((int32_t)(now - alarm_next_toggle) >= 0) {
        alarm_phase = !alarm_phase;
        led_rgb(alarm_phase ? STATUS_LED_BRIGHTNESS : 0, 0, 0);
        alarm_next_toggle = now + pdMS_TO_TICKS(100U);
    }
}

static void init_gate_for_ulp(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << LOCK_GATE_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_ERROR_CHECK(gpio_set_level(LOCK_GATE_GPIO, 0));
    rtc_gpio_init(LOCK_GATE_GPIO);
    rtc_gpio_hold_dis(LOCK_GATE_GPIO);
    rtc_gpio_set_direction(LOCK_GATE_GPIO, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(LOCK_GATE_GPIO, 0);
}

static void init_adc_for_ulp(void)
{
    adc_oneshot_unit_handle_t adc;
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_RISCV,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc));
    adc_oneshot_chan_cfg_t cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, ADC_CHANNEL_6, &cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, ADC_CHANNEL_4, &cfg));
    // Keep the attenuation explicit in the SAR registers as well. The ULP
    // low-level channel switch does not always preserve the HP oneshot setup.
    adc_oneshot_ll_set_atten(ADC_UNIT_1, ADC_CHANNEL_6, ADC_ATTEN_DB_12);
    adc_oneshot_ll_set_atten(ADC_UNIT_1, ADC_CHANNEL_4, ADC_ATTEN_DB_12);
    adc_set_hw_calibration_code(ADC_UNIT_1, ADC_ATTEN_DB_12);
    // Ownership remains with ULP after this initialization; main never reads
    // or deletes the ADC unit.
}

static void init_ulp(void)
{
    // A flash/reset cycle does not necessarily clear the RTC domain and the
    // ULP can still be running the previous image. Stop and reset it before
    // touching RTC GPIO/ADC or replacing the binary in RTC slow memory.
    ulp_riscv_timer_stop();
    ulp_riscv_halt();
    ulp_riscv_reset();
    ESP_ERROR_CHECK(ulp_riscv_load_binary(ulp_main_bin_start,
                                          ulp_main_bin_end - ulp_main_bin_start));
    // RTC slow memory survives parts of the reset/flash sequence. Explicitly
    // clear the mailbox and status contract so a previous fault/event cannot
    // be mistaken for an event from the current ULP run.
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
    // The ULP itself retains the 50 ms off-guard. A zero wakeup period avoids
    // adding a second 50 ms delay between diagnostic pulses.
    ESP_ERROR_CHECK(ulp_set_wakeup_period(0, ULP_MEASURE_PERIOD_US));
    ESP_ERROR_CHECK(ulp_riscv_run());
}

static bool read_snapshot(uint32_t *seq, int32_t *base, int32_t *vbat,
                          int32_t *sense, int32_t *delta, uint32_t *feature,
                          uint32_t *state, uint32_t *faults, uint32_t *count,
                          uint32_t *controller_state, uint32_t *ack_seq,
                          uint32_t *fsm, uint32_t *event_seq,
                          uint32_t *events, uint32_t *coil_current,
                          uint32_t *close_state, uint32_t *close_feature,
                          int32_t *close_delta, uint32_t *close_attempts)
{
    for (int tries = 0; tries < 4; tries++) {
        const uint32_t before = ulp_sequence;
        if (before & 1u) continue;
        *base = ulp_baseline_raw; *vbat = ulp_vbat_raw;
        *sense = ulp_sense_raw; *delta = ulp_delta_raw;
        *feature = ulp_normalized_feature; *state = ulp_plate_state;
        *faults = ulp_fault_flags; *count = ulp_measurement_counter;
        *controller_state = ulp_lock_state;
        *ack_seq = ulp_command_ack_sequence;
        *fsm = ulp_fsm_state;
        *event_seq = ulp_event_sequence;
        *events = ulp_event_flags;
        *coil_current = ulp_coil_current_raw;
        *close_state = ulp_close_verify_state;
        *close_feature = ulp_close_verify_feature;
        *close_delta = ulp_close_verify_delta_raw;
        *close_attempts = ulp_close_attempt_counter;
        const uint32_t after = ulp_sequence;
        if (before == after && !(after & 1u)) { *seq = after; return true; }
    }
    return false;
}

void app_main(void)
{
    // Stop a possibly surviving ULP before the first GPIO/ADC initialization.
    // init_ulp repeats this immediately before loading the new image; the
    // early stop closes the race during boot-time peripheral setup.
    ulp_riscv_timer_stop();
    ulp_riscv_halt();
    ulp_riscv_reset();
    init_gate_for_ulp();
    init_adc_for_ulp();
    led_init();
    led_rgb(0, 0, 0);
    init_ulp();
    lock_controller_request(ULP_CMD_UNLOCK);

    uint32_t last_count = UINT32_MAX;
    while (true) {
#if ULP_TEST_CYCLE_ENABLE
        run_test_command_cycle();
#endif
        service_fault_alarm();
        uint32_t seq, feature, state, faults, count, controller_state, ack_seq;
        uint32_t fsm, event_seq, events, coil_current;
        uint32_t close_state, close_feature, close_attempts;
        int32_t base, vbat, sense, delta, close_delta;
        if (read_snapshot(&seq, &base, &vbat, &sense, &delta, &feature,
                          &state, &faults, &count, &controller_state,
                          &ack_seq, &fsm, &event_seq, &events,
                          &coil_current, &close_state, &close_feature,
                          &close_delta, &close_attempts)) {
            if (event_seq != handled_event_sequence) {
                handled_event_sequence = event_seq;
                if (events & ULP_EVENT_COIL_FAULT) start_fault_alarm();
            }
            if (count != last_count) {
                last_count = count;
            // Plate colours are meaningful only while the controller is in
            // UNLOCKED mode. The locked state has no fresh plate sample.
            // The fault alarm owns the LED until it expires. Do not let the
            // normal plate-state renderer overwrite the alarm indication in
            // the same loop in which the ULP fault event is consumed.
            if (!alarm_active) {
                if (controller_state == LOCK_STATE_UNLOCKED) {
                    led_state(state);
                } else {
                    led_rgb(0, 0, 0);
                }
            }
            ESP_LOGI(TAG, "lock=%" PRIu32 ",fsm=%" PRIu32 ",plate=%" PRIu32
                     ",base=%" PRId32 ",sense=%" PRId32 ",delta=%" PRId32
                     ",feature=%" PRIu32 ",coil=%" PRIu32
                     ",fault=0x%08" PRIx32 ",event=0x%08" PRIx32
                     ",close_state=%" PRIu32 ",close_feature=%" PRIu32
                     ",close_delta=%" PRId32 ",close_attempts=%" PRIu32,
                     controller_state, fsm, state, base, sense, delta, feature,
                     coil_current, faults, events, close_state, close_feature,
                     close_delta, close_attempts);
            }
        }
        (void)seq; (void)base; (void)vbat; (void)sense; (void)delta;
        (void)feature; (void)faults;
        (void)controller_state; (void)ack_seq;
        (void)fsm; (void)event_seq; (void)events; (void)coil_current;
        (void)close_state; (void)close_feature; (void)close_delta;
        (void)close_attempts;
        (void)ulp_event_pending;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
