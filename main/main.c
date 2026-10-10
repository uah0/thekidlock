#include <stdio.h>
#include <string.h>
#include "ap_mode.h"
#include "app_config.h"
#include "app_diagnostics.h"
#include "ble_phone.h"
#include "led.h"
#include "lock_control.h"
#include "ulp_controller.h"
#include "event_log.h"
#include "cloud_link.h"
#include "remote_log.h"
#include "wifi_ota.h"
#include "esp_err.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "main";
static volatile uint32_t s_buzzer_test_request;
static portMUX_TYPE s_departure_preview_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_departure_preview_pulse_ms;
static int64_t s_departure_preview_until_us;
static bool s_departure_preview_ready;
static volatile bool s_ulp_diagnostics_verbose;

#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
static bool s_startup_critical_ok;

static void ota_boot_confirmation_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(5000));

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    esp_err_t state_err = running ? esp_ota_get_state_partition(running, &state)
                                  : ESP_ERR_NOT_FOUND;
    if (state_err != ESP_OK || state != ESP_OTA_IMG_PENDING_VERIFY) {
        vTaskDelete(NULL);
        return;
    }

    bool healthy = true;
#if CONFIG_SECURE_BOOT
    healthy = s_startup_critical_ok;
#endif
#if CONFIG_APP_FINALIZE_ROM_DOWNLOAD_AFTER_OTA
    ulp_diagnostics_t diagnostics;
    healthy = healthy && ulp_controller_read_diagnostics(&diagnostics) &&
        diagnostics.calibration_valid && !diagnostics.calibration_mode &&
        diagnostics.fault_flags == 0;
#endif
    if (!healthy) {
        ESP_LOGE(TAG, "OTA boot self-check failed; rolling back to previous app");
        event_log_add("OTA: NVS/ULP/BLE не прошли стартовую проверку; откат");
        esp_ota_mark_app_invalid_rollback_and_reboot();
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA boot self-check passed; image marked valid");
        event_log_add("OTA: ULP и BLE запущены, образ подтверждён");
#if CONFIG_APP_FINALIZE_ROM_DOWNLOAD_AFTER_OTA
        if (!esp_efuse_read_field_bit(ESP_EFUSE_DIS_DOWNLOAD_MODE)) {
            esp_err_t efuse_err = esp_efuse_disable_rom_download_mode();
            bool disabled = esp_efuse_read_field_bit(ESP_EFUSE_DIS_DOWNLOAD_MODE);
            if (efuse_err == ESP_OK && disabled) {
                ESP_LOGI(TAG, "ROM Download permanently disabled after OTA self-check");
                event_log_add("Security: ROM Download отключён после проверки OTA");
            } else {
                ESP_LOGE(TAG, "ROM Download disable failed: err=%s state=%d",
                         esp_err_to_name(efuse_err), disabled);
                event_log_add("Security: не удалось отключить ROM Download");
            }
        }
#endif
    } else {
        ESP_LOGE(TAG, "OTA image confirmation failed: %s", esp_err_to_name(err));
    }
    vTaskDelete(NULL);
}
#endif

static esp_err_t init_buzzer_off(void)
{
    const int inactive_level = !APP_BUZZER_ACTIVE_LEVEL;
    esp_err_t err = gpio_set_level(APP_BUZZER_GPIO, inactive_level);
    if (err != ESP_OK) return err;

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << APP_BUZZER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&cfg);
    if (err != ESP_OK) return err;
    return gpio_set_level(APP_BUZZER_GPIO, inactive_level);
}

static esp_err_t init_nvs(void)
{
    /* Never erase calibration, owner and bonds as an automatic recovery. */
    return nvs_flash_init();
}

static esp_err_t configuration_start(void)
{
    ble_phone_allow_registration();
    led_set_configuration(true);
    ap_mode_set_configuration(true);
    event_log_add("Режим настройки BLE включён");
    return ESP_OK;
}

static esp_err_t configuration_stop(void)
{
    ble_phone_set_registration_mode(false);

    ap_mode_set_configuration(false);
    led_set_configuration(false);
    event_log_add("Режим настройки BLE выключен");
    return ESP_OK;
}

static void mode_button_callback(bool configuration_mode, bool erase_bonds)
{
    if (erase_bonds) {
        esp_err_t err = ble_phone_delete_all_bonds();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "delete all bonds failed: %s", esp_err_to_name(err));
            event_log_add("GPIO0: ошибка удаления bonds: %s", esp_err_to_name(err));
        } else {
            event_log_add("GPIO0: все bonds удалены длинным нажатием");
        }
        return;
    }

    esp_err_t err = configuration_mode ? configuration_start() : configuration_stop();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "configuration mode switch failed: %s", esp_err_to_name(err));
        event_log_add("GPIO0: ошибка переключения режима настройки: %s",
                      esp_err_to_name(err));
    }
}

static void diagnostic_console_task(void *arg)
{
    (void)arg;
    char command[32];

    ESP_LOGI(TAG, "COM diagnostic ready; send GPIO0 followed by Enter");
    for (;;) {
        if (!fgets(command, sizeof(command), stdin)) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        command[strcspn(command, "\r\n")] = '\0';
        if (strcmp(command, "GPIO0") == 0) {
            bool enable = !ap_mode_is_configuration();
            ESP_LOGI(TAG, "COM command GPIO0: toggle configuration mode to %s",
                     enable ? "on" : "off");
            event_log_add("COM29: имитация короткого нажатия GPIO0");
            mode_button_callback(enable, false);
        } else if (command[0] != '\0') {
            ESP_LOGW(TAG, "Unknown COM diagnostic command: %s", command);
        }
    }
}

static const char *diagnostic_plate_state_name(uint32_t state)
{
    switch ((plate_state_t)state) {
        case PLATE_CONTACT: return "CONTACT";
        case PLATE_GAP_1: return "GAP_1";
        case PLATE_GAP_2: return "GAP_2";
        case PLATE_GAP_3: return "GAP_3";
        case PLATE_GAP_4: return "GAP_4";
        case PLATE_GAP_5: return "GAP_5";
        case PLATE_FAR: return "FAR";
        default: return "UNKNOWN";
    }
}

void app_ulp_diagnostics_set_verbose(bool enabled)
{
    s_ulp_diagnostics_verbose = enabled;
    ESP_LOGI("ulp_diag", "verbose=%u", enabled ? 1u : 0u);
}

bool app_ulp_diagnostics_get_verbose(void)
{
    return s_ulp_diagnostics_verbose;
}

static void ulp_gate_diagnostic_task(void *arg)
{
    (void)arg;
    uint32_t previous_high = UINT32_MAX;
    uint32_t previous_low = UINT32_MAX;
    ulp_diagnostics_t previous = {0};
    bool have_previous = false;

    for (;;) {
        ulp_diagnostics_t d;
        if (ulp_controller_read_diagnostics(&d)) {
            bool significant = !have_previous ||
                d.requested_command != previous.requested_command ||
                d.command_sequence != previous.command_sequence ||
                d.command_ack_sequence != previous.command_ack_sequence ||
                d.command_overlap_counter != previous.command_overlap_counter ||
                d.calibration_mode != previous.calibration_mode ||
                d.calibration_verify_counter != previous.calibration_verify_counter ||
                d.lock_state != previous.lock_state ||
                d.fsm_state != previous.fsm_state ||
                d.plate_state != previous.plate_state ||
                d.fault_flags != previous.fault_flags ||
                d.event_flags != previous.event_flags ||
                d.close_attempt_counter != previous.close_attempt_counter ||
                d.close_reject_counter != previous.close_reject_counter ||
                d.close_reject_latched != previous.close_reject_latched ||
                d.close_verify_state != previous.close_verify_state ||
                d.event_sequence != previous.event_sequence ||
                (d.fsm_state == LOCK_FSM_GATE_TEST &&
                 d.sense_raw != previous.sense_raw);
            bool verbose_edge = s_ulp_diagnostics_verbose &&
                (d.gate_high_counter != previous_high ||
                 d.gate_low_counter != previous_low);
            if (!significant && !verbose_edge) {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            const int32_t verify_offset =
                d.calibration_verify_contact_raw - d.calibration_contact_raw;
            int32_t verify_adjusted = d.close_verify_delta_raw - verify_offset;
            if (verify_adjusted < 0) verify_adjusted = 0;
            ESP_LOGI("ulp_gate",
                     "GPIO9=%lu high_edges=%lu low_edges=%lu cmd=%lu seq=%lu ack=%lu overlap=%lu "
                     "cal=%lu lock=%u fsm=%u plate=%u faults=0x%lx events=0x%lx "
                     "delta=%ld feature=%lu measure=%lu close=%lu reject=%lu latched=%lu "
                     "verify_state=%lu verify_delta=%ld",
                     (unsigned long)d.gate_level,
                     (unsigned long)d.gate_high_counter,
                     (unsigned long)d.gate_low_counter,
                     (unsigned long)d.requested_command,
                     (unsigned long)d.command_sequence,
                     (unsigned long)d.command_ack_sequence,
                     (unsigned long)d.command_overlap_counter,
                     (unsigned long)d.calibration_mode,
                     (unsigned)d.lock_state, (unsigned)d.fsm_state,
                     (unsigned)d.plate_state, (unsigned long)d.fault_flags,
                     (unsigned long)d.event_flags, (long)d.delta_raw,
                     (unsigned long)d.normalized_feature,
                     (unsigned long)d.measurement_counter,
                     (unsigned long)d.close_attempt_counter,
                     (unsigned long)d.close_reject_counter,
                     (unsigned long)d.close_reject_latched,
                     (unsigned long)d.close_verify_state,
                     (long)d.close_verify_delta_raw);
            ESP_LOGI("ulp_gap",
                     "ADC baseline=%ld sense=%ld delta=%ld vbat=%ld pulse_us=%lu "
                     "cal_raw={contact:%ld paper1:%ld paper2:%ld open:%ld hot_contact:%ld}",
                     (long)d.baseline_raw, (long)d.sense_raw, (long)d.delta_raw,
                     (long)d.vbat_raw, (unsigned long)d.pulse_actual_us,
                     (long)d.calibration_contact_raw,
                     (long)d.calibration_paper1_raw,
                     (long)d.calibration_paper2_raw,
                     (long)d.calibration_open_raw,
                     (long)d.calibration_verify_contact_raw);
            ESP_LOGI("ulp_gap",
                     "CLASS scan={state:%u/%s feature:%lu} close_detected=%lu/%s "
                     "verify={raw:%ld offset:%ld "
                     "adjusted:%ld state:%lu/%s feature:%lu} thresholds={contact:%u "
                     "gap1:%u gap2:%u gap3:%u gap4:%u gap5:%u far_on:%u far_off:%u}",
                     (unsigned)d.plate_state,
                     diagnostic_plate_state_name(d.plate_state),
                     (unsigned long)d.normalized_feature,
                     (unsigned long)d.close_detected_state,
                     diagnostic_plate_state_name(d.close_detected_state),
                     (long)d.close_verify_delta_raw, (long)verify_offset,
                     (long)verify_adjusted, (unsigned long)d.close_verify_state,
                     diagnostic_plate_state_name(d.close_verify_state),
                     (unsigned long)d.close_verify_feature,
                     ULP_FEATURE_CONTACT_MAX, ULP_FEATURE_GAP1_MAX,
                     ULP_FEATURE_GAP2_MAX, ULP_FEATURE_GAP3_MAX,
                     ULP_FEATURE_GAP4_MAX, ULP_FEATURE_GAP5_MAX,
                     ULP_FEATURE_FAR_ON, ULP_FEATURE_FAR_OFF);
            previous_high = d.gate_high_counter;
            previous_low = d.gate_low_counter;
            previous = d;
            have_previous = true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static unsigned close_failure_beep_count(uint32_t plate_state)
{
    switch ((plate_state_t)plate_state) {
        case PLATE_GAP_1: return 1;
        case PLATE_GAP_2: return 2;
        case PLATE_GAP_3: return 3;
        case PLATE_GAP_4: return 4;
        case PLATE_GAP_5: return 5;
        case PLATE_FAR:
        case PLATE_UNKNOWN: return 6;
        default: return 0;
    }
}

static void buzzer_set(bool enabled)
{
    static int last_level = -1;
    const int requested_level = enabled
        ? APP_BUZZER_ACTIVE_LEVEL : !APP_BUZZER_ACTIVE_LEVEL;
    gpio_set_level(APP_BUZZER_GPIO, requested_level);
    if (requested_level != last_level) {
        ESP_LOGI("buzzer", "GPIO8 enabled=%u requested=%d read=%d",
                 enabled ? 1u : 0u, requested_level,
                 gpio_get_level(APP_BUZZER_GPIO));
        last_level = requested_level;
    }
}

static bool close_failure_alarm_active(ulp_diagnostics_t *d, unsigned *count)
{
    if (!ulp_controller_read_diagnostics(d) || d->calibration_mode ||
        d->requested_command != ULP_CMD_LOCK ||
        d->lock_state == LOCK_STATE_LOCKED || !d->close_reject_latched) {
        return false;
    }
    *count = close_failure_beep_count(d->close_verify_state);
    return *count != 0;
}

static bool beacon_departure_alarm_active(uint32_t *pulse_ms)
{
    app_config_t cfg;
    lock_feedback_t feedback;
    app_config_get(&cfg);
    if (!cfg.departure_buzzer_enabled) return false;
    *pulse_ms = cfg.departure_buzzer_pulse_ms;
    lock_control_get_feedback(&feedback);
    return feedback.valid && feedback.state == LOCK_STATE_UNLOCKED &&
           !feedback.hold_open;
}

static bool buzzer_wait(uint32_t duration_ms, uint32_t overlap_seen)
{
    uint32_t elapsed = 0;
    while (elapsed < duration_ms) {
        const uint32_t slice = duration_ms - elapsed > 25 ? 25 : duration_ms - elapsed;
        vTaskDelay(pdMS_TO_TICKS(slice));
        elapsed += slice;
        ulp_diagnostics_t d;
        if (ulp_controller_read_diagnostics(&d) &&
            d.command_overlap_counter != overlap_seen) {
            return false;
        }
    }
    return true;
}

static bool buzzer_tone(uint32_t on_ms, uint32_t off_ms, uint32_t overlap_seen)
{
    buzzer_set(true);
    if (!buzzer_wait(on_ms, overlap_seen)) {
        buzzer_set(false);
        return false;
    }
    buzzer_set(false);
    return buzzer_wait(off_ms, overlap_seen);
}

// Preserve the original 20 ms + eight 25 ms slices after tick conversion.
static void departure_pulse_off(void *arg)
{
    (void)arg;
    gpio_set_level(APP_BUZZER_GPIO, !APP_BUZZER_ACTIVE_LEVEL);
}

static bool departure_buzzer_tone(esp_timer_handle_t timer, uint32_t pulse_ms,
                                 uint32_t overlap_seen)
{
    if (!timer) return false;
    TickType_t wake = xTaskGetTickCount();
    gpio_set_level(APP_BUZZER_GPIO, APP_BUZZER_ACTIVE_LEVEL);
    esp_err_t err = esp_timer_start_once(timer, (uint64_t)pulse_ms * 1000);
    if (err != ESP_OK) {
        gpio_set_level(APP_BUZZER_GPIO, !APP_BUZZER_ACTIVE_LEVEL);
        ESP_LOGE("buzzer", "Pulse timer start failed: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(100));
        return false;
    }
    const TickType_t period = pdMS_TO_TICKS(20) + 8 * pdMS_TO_TICKS(25);
    TickType_t remaining = period;
    bool completed = true;
    while (remaining) {
        const TickType_t slice = remaining > pdMS_TO_TICKS(20)
            ? pdMS_TO_TICKS(20) : remaining;
        vTaskDelayUntil(&wake, slice);
        remaining -= slice;
        ulp_diagnostics_t d;
        if (ulp_controller_read_diagnostics(&d) &&
            d.command_overlap_counter != overlap_seen) {
            completed = false;
            break;
        }
    }
    (void)esp_timer_stop(timer);
    gpio_set_level(APP_BUZZER_GPIO, !APP_BUZZER_ACTIVE_LEVEL);
    return completed;
}

static void buzzer_overlap_pattern(uint32_t overlap_seen)
{
    ESP_LOGW("buzzer", "overlap=%lu: pattern DASH-DOT-DOT-DOT",
             (unsigned long)overlap_seen);
    if (!buzzer_tone(600, 200, overlap_seen)) return;
    for (unsigned i = 0; i < 3; i++) {
        if (!buzzer_tone(150, i == 2 ? 600 : 150, overlap_seen)) return;
    }
}

esp_err_t app_buzzer_request_test(uint32_t pattern)
{
    if (pattern < 1 || pattern > 7) return ESP_ERR_INVALID_ARG;
    s_buzzer_test_request = pattern;
    return ESP_OK;
}

esp_err_t app_buzzer_request_departure_preview(uint32_t pulse_ms)
{
    if (pulse_ms < APP_DEPARTURE_PULSE_MIN_MS ||
        pulse_ms > APP_DEPARTURE_PULSE_MAX_MS) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_departure_preview_mux);
    if (!s_departure_preview_ready) {
        portEXIT_CRITICAL(&s_departure_preview_mux);
        return ESP_ERR_INVALID_STATE;
    }
    s_departure_preview_pulse_ms = pulse_ms;
    s_departure_preview_until_us = esp_timer_get_time() + 5000000;
    portEXIT_CRITICAL(&s_departure_preview_mux);
    return ESP_OK;
}

static bool departure_preview_active(uint32_t *pulse_ms)
{
    portENTER_CRITICAL(&s_departure_preview_mux);
    int64_t until_us = s_departure_preview_until_us;
    uint32_t width_ms = s_departure_preview_pulse_ms;
    portEXIT_CRITICAL(&s_departure_preview_mux);
    const int64_t remaining_ms = (until_us - esp_timer_get_time()) / 1000;
    if (remaining_ms <= 0) return false;
    *pulse_ms = remaining_ms < width_ms ? (uint32_t)remaining_ms : width_ms;
    return true;
}

static void close_failure_buzzer_task(void *arg)
{
    (void)arg;
    esp_timer_handle_t departure_timer = NULL;
    const esp_timer_create_args_t timer_args = {
        .callback = departure_pulse_off,
        .name = "departure_pulse",
    };
    esp_err_t timer_err = esp_timer_create(&timer_args, &departure_timer);
    if (timer_err != ESP_OK) {
        ESP_LOGE("buzzer", "Pulse timer create failed: %s", esp_err_to_name(timer_err));
    }
    portENTER_CRITICAL(&s_departure_preview_mux);
    s_departure_preview_ready = timer_err == ESP_OK;
    portEXIT_CRITICAL(&s_departure_preview_mux);
    ulp_diagnostics_t d;
    uint32_t overlap_seen = 0;
    if (ulp_controller_read_diagnostics(&d)) {
        overlap_seen = d.command_overlap_counter;
    }
    for (;;) {
        uint32_t test_pattern = s_buzzer_test_request;
        if (test_pattern != 0) {
            s_buzzer_test_request = 0;
            ESP_LOGW("buzzer", "manual test pattern=%lu",
                     (unsigned long)test_pattern);
            if (test_pattern == 7) {
                buzzer_overlap_pattern(overlap_seen);
            } else {
                for (uint32_t i = 0; i < test_pattern; i++) {
                    if (!buzzer_tone(200, 200, overlap_seen)) break;
                }
                buzzer_set(false);
            }
            continue;
        }

        if (ulp_controller_read_diagnostics(&d) &&
            d.command_overlap_counter != overlap_seen) {
            overlap_seen = d.command_overlap_counter;
            buzzer_overlap_pattern(overlap_seen);
            continue;
        }

        unsigned count = 0;
        if (!close_failure_alarm_active(&d, &count)) {
            uint32_t departure_pulse_ms;
            if (departure_preview_active(&departure_pulse_ms) ||
                beacon_departure_alarm_active(&departure_pulse_ms)) {
                if (!departure_buzzer_tone(departure_timer, departure_pulse_ms, overlap_seen)) {
                    vTaskDelay(pdMS_TO_TICKS(20));
                    continue;
                }
            } else {
                buzzer_set(false);
                (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
            }
            continue;
        }

        ESP_LOGI("buzzer", "gap pattern start: count=%u reject=%lu",
                 count, (unsigned long)d.close_reject_counter);
        for (unsigned i = 0; i < count; i++) {
            unsigned current_count = 0;
            if (!close_failure_alarm_active(&d, &current_count)) break;
            if (!buzzer_tone(200, 200, overlap_seen)) break;
        }
        buzzer_set(false);
        (void)buzzer_wait(1200, overlap_seen);
    }
}

void app_main(void)
{
    bool ble_initialized = false;
    remote_log_init();
    event_log_init();
    event_log_add("Замок: запуск, прошивка %s", APP_VERSION);
    ESP_ERROR_CHECK_WITHOUT_ABORT(init_buzzer_off());
    esp_err_t nvs_err = init_nvs();
    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_err);
    ESP_ERROR_CHECK_WITHOUT_ABORT(app_config_init());
    if (nvs_err == ESP_OK) ESP_ERROR_CHECK_WITHOUT_ABORT(cloud_link_init());
    /* Control first; ancillary failures must not reset the coil/ULP. */
    esp_err_t ulp_err = ulp_controller_init();
    ESP_ERROR_CHECK_WITHOUT_ABORT(ulp_err);
    ESP_ERROR_CHECK_WITHOUT_ABORT(led_init());
    led_set_state(LED_STATE_BOOT);
    if (ulp_err == ESP_OK) ESP_ERROR_CHECK_WITHOUT_ABORT(lock_control_init());
    else led_set_state(LED_STATE_FAULT);
    esp_err_t wifi_err = wifi_ota_init();
    ESP_ERROR_CHECK_WITHOUT_ABORT(wifi_err);
    if (wifi_err == ESP_OK) ESP_ERROR_CHECK_WITHOUT_ABORT(cloud_link_start());
    ESP_ERROR_CHECK_WITHOUT_ABORT(xTaskCreate(close_failure_buzzer_task, "close_buzzer", 4096,
                                NULL, 3, NULL) == pdPASS
                                ? ESP_OK : ESP_ERR_NO_MEM);
    /* No persistence means no secure bond/owner provisioning. */
    if (nvs_err == ESP_OK) {
        esp_err_t ble_err = ble_phone_init();
        ESP_ERROR_CHECK_WITHOUT_ABORT(ble_err);
        ble_initialized = ble_err == ESP_OK;
    }
    if (ble_initialized) ESP_ERROR_CHECK_WITHOUT_ABORT(ap_mode_init(mode_button_callback));
    ESP_ERROR_CHECK_WITHOUT_ABORT(xTaskCreate(diagnostic_console_task, "com_diag", 6144, NULL,
                                4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK_WITHOUT_ABORT(xTaskCreate(ulp_gate_diagnostic_task, "ulp_gate_diag", 4096,
                                NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    esp_err_t web_err = wifi_err == ESP_OK ? wifi_ota_start_web() : wifi_err;
    if (web_err != ESP_OK && web_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Web UI start not scheduled: %s", esp_err_to_name(web_err));
    }
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    s_startup_critical_ok = nvs_err == ESP_OK && ulp_err == ESP_OK && ble_initialized;
    if (xTaskCreate(ota_boot_confirmation_task, "ota_boot_check", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "OTA boot self-check task could not start; requesting rollback");
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
#endif
    event_log_add("Startup: ULP=%s BLE=%d WiFi=%s NVS=%s", esp_err_to_name(ulp_err),
                  ble_initialized, esp_err_to_name(wifi_err), esp_err_to_name(nvs_err));
}
