#include "app_config.h"

#include <string.h>
#include "driver/gpio.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "remote_log.h"

#define NVS_NS "blelock"
static const char *TAG = "app_config";

static app_config_t s_cfg;
static SemaphoreHandle_t s_config_mutex;

static void set_defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.phone_rssi_threshold = APP_DEFAULT_PHONE_RSSI_THRESHOLD;
    s_cfg.timer_a_ms = APP_DEFAULT_TIMER_A_MS;
    s_cfg.beacon_reopen_protection_ms = APP_DEFAULT_BLE_REOPEN_PROTECTION_MS;
    s_cfg.shunt_gpio = APP_DEFAULT_SHUNT_GPIO;
    s_cfg.serial_log_enabled = true;
    s_cfg.departure_buzzer_enabled = true;
    s_cfg.departure_buzzer_pulse_ms = APP_DEFAULT_DEPARTURE_PULSE_MS;
    s_cfg.configuration_mode = false;
}

static bool values_valid(int phone_rssi, uint32_t timer_a_ms,
                         uint32_t beacon_reopen_protection_ms)
{
    return phone_rssi >= APP_RSSI_THRESHOLD_MIN &&
           phone_rssi <= APP_RSSI_THRESHOLD_MAX &&
           timer_a_ms <= APP_TIMER_A_MS_MAX &&
           beacon_reopen_protection_ms >= APP_BLE_REOPEN_PROTECTION_MS_MIN &&
           beacon_reopen_protection_ms <= APP_BLE_REOPEN_PROTECTION_MS_MAX &&
           beacon_reopen_protection_ms % APP_BLE_REOPEN_PROTECTION_MS_STEP == 0;
}

static bool departure_pulse_valid(uint32_t pulse_ms)
{
    return pulse_ms <= APP_DEPARTURE_PULSE_MAX_MS &&
           pulse_ms % APP_DEPARTURE_PULSE_STEP_MS == 0;
}

esp_err_t app_config_init(void)
{
    set_defaults();
    if (!s_config_mutex) s_config_mutex = xSemaphoreCreateMutex();
    if (!s_config_mutex) return ESP_ERR_NO_MEM;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    int8_t phone_rssi = s_cfg.phone_rssi_threshold;
    uint32_t timer_a_ms = s_cfg.timer_a_ms;
    uint32_t beacon_reopen_protection_ms = s_cfg.beacon_reopen_protection_ms;
    uint8_t serial_log = 1;
    uint8_t departure_buzzer = 1;
    uint32_t departure_pulse_ms = APP_DEFAULT_DEPARTURE_PULSE_MS;
    nvs_get_i8(h, "phone_rssi", &phone_rssi);
    nvs_get_u32(h, "timer_a_ms", &timer_a_ms);
    esp_err_t protection_err = nvs_get_u32(h, "ble_reopen_ms", &beacon_reopen_protection_ms);
    if (protection_err == ESP_ERR_NVS_NOT_FOUND ||
        (protection_err == ESP_OK &&
         (beacon_reopen_protection_ms < APP_BLE_REOPEN_PROTECTION_MS_MIN ||
          beacon_reopen_protection_ms > APP_BLE_REOPEN_PROTECTION_MS_MAX ||
          beacon_reopen_protection_ms % APP_BLE_REOPEN_PROTECTION_MS_STEP != 0))) {
        beacon_reopen_protection_ms = APP_DEFAULT_BLE_REOPEN_PROTECTION_MS;
        protection_err = nvs_set_u32(h, "ble_reopen_ms", beacon_reopen_protection_ms);
        if (protection_err == ESP_OK) protection_err = nvs_commit(h);
    }
    if (protection_err != ESP_OK) {
        nvs_close(h);
        return protection_err;
    }
    nvs_get_u8(h, "serial_log", &serial_log);
    err = nvs_get_u8(h, "departure_beep", &departure_buzzer);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_set_u8(h, "departure_beep", departure_buzzer);
        if (err == ESP_OK) err = nvs_commit(h);
    }
    if (err == ESP_OK) {
        err = nvs_get_u32(h, "departure_ms", &departure_pulse_ms);
        if (err == ESP_OK && departure_pulse_ms > APP_DEPARTURE_PULSE_MAX_MS &&
            departure_pulse_ms <= 160) {
            departure_pulse_ms = APP_DEPARTURE_PULSE_MAX_MS;
            err = nvs_set_u32(h, "departure_ms", departure_pulse_ms);
            if (err == ESP_OK) err = nvs_commit(h);
        }
        if (err == ESP_ERR_NVS_NOT_FOUND ||
            (err == ESP_OK && !departure_pulse_valid(departure_pulse_ms))) {
            departure_pulse_ms = APP_DEFAULT_DEPARTURE_PULSE_MS;
            err = nvs_set_u32(h, "departure_ms", departure_pulse_ms);
            if (err == ESP_OK) err = nvs_commit(h);
        }
    }
    nvs_close(h);
    if (err != ESP_OK) return err;

    nvs_handle_t cleanup;
    if (nvs_open(NVS_NS, NVS_READWRITE, &cleanup) == ESP_OK) {
        esp_err_t old_long_open_s = nvs_erase_key(cleanup, "long_open_s");
        esp_err_t old_long_open_ms = nvs_erase_key(cleanup, "long_open_ms");
        esp_err_t old_ble_tx_power = nvs_erase_key(cleanup, "ble_tx_power");
        if (old_long_open_s == ESP_OK || old_long_open_ms == ESP_OK ||
            old_ble_tx_power == ESP_OK) {
            esp_err_t cleanup_err = nvs_commit(cleanup);
            if (cleanup_err != ESP_OK) {
                ESP_LOGW(TAG, "Не удалось удалить устаревшую настройку long-open/BLE TX: %s",
                         esp_err_to_name(cleanup_err));
            }
        }
        nvs_close(cleanup);
    }

    if (values_valid(phone_rssi, timer_a_ms, beacon_reopen_protection_ms)) {
        s_cfg.phone_rssi_threshold = phone_rssi;
        s_cfg.timer_a_ms = timer_a_ms;
        s_cfg.beacon_reopen_protection_ms = beacon_reopen_protection_ms;
    }
    s_cfg.serial_log_enabled = serial_log != 0;
    s_cfg.departure_buzzer_enabled = departure_buzzer != 0 && departure_pulse_ms != 0;
    s_cfg.departure_buzzer_pulse_ms = departure_pulse_ms;
    remote_log_set_serial_enabled(s_cfg.serial_log_enabled);
    return ESP_OK;
}

void app_config_get(app_config_t *out)
{
    if (!out) return;
    if (s_config_mutex) xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    *out = s_cfg;
    if (s_config_mutex) xSemaphoreGive(s_config_mutex);
}

static esp_err_t set_logic_locked(int phone_rssi, uint32_t timer_a_ms,
                                  uint32_t beacon_reopen_protection_ms)
{
    if (!values_valid(phone_rssi, timer_a_ms, beacon_reopen_protection_ms)) {
        ESP_LOGE(TAG, "logic validation failed: zone=%d A=%lu reopen=%lu",
                 phone_rssi, (unsigned long)timer_a_ms,
                 (unsigned long)beacon_reopen_protection_ms);
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    if (err == ESP_OK) err = nvs_set_i8(h, "phone_rssi", phone_rssi);
    if (err == ESP_OK) err = nvs_set_u32(h, "timer_a_ms", timer_a_ms);
    if (err == ESP_OK) err = nvs_set_u32(h, "ble_reopen_ms", beacon_reopen_protection_ms);
    if (err == ESP_OK) {
        nvs_erase_key(h, "long_open_s");
        nvs_erase_key(h, "long_open_ms");
        nvs_erase_key(h, "lock_gpio");
        nvs_erase_key(h, "lock_open");
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    if (err == ESP_OK) {
        s_cfg.phone_rssi_threshold = phone_rssi;
        s_cfg.timer_a_ms = timer_a_ms;
        s_cfg.beacon_reopen_protection_ms = beacon_reopen_protection_ms;
    }
    return err;
}

esp_err_t app_config_set_logic(int phone_rssi, uint32_t timer_a_ms,
                               uint32_t beacon_reopen_protection_ms)
{
    if (!s_config_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    esp_err_t err = set_logic_locked(phone_rssi, timer_a_ms,
                                     beacon_reopen_protection_ms);
    xSemaphoreGive(s_config_mutex);
    return err;
}
esp_err_t app_config_set_serial_log_enabled(bool enabled)
{
    if (!s_config_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) err = nvs_set_u8(h, "serial_log", enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(h);
    if (h) nvs_close(h);
    if (err == ESP_OK) {
        s_cfg.serial_log_enabled = enabled;
        remote_log_set_serial_enabled(enabled);
    }
    xSemaphoreGive(s_config_mutex);
    return err;
}

esp_err_t app_config_set_departure_buzzer_enabled(bool enabled)
{
    if (!s_config_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) err = nvs_set_u8(h, "departure_beep", enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(h);
    if (h) nvs_close(h);
    if (err == ESP_OK) s_cfg.departure_buzzer_enabled = enabled;
    xSemaphoreGive(s_config_mutex);
    return err;
}

esp_err_t app_config_set_departure_buzzer_pulse_ms(uint32_t pulse_ms)
{
    if (!departure_pulse_valid(pulse_ms)) return ESP_ERR_INVALID_ARG;
    if (!s_config_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    bool enabled = pulse_ms != 0;
    if (s_cfg.departure_buzzer_pulse_ms == pulse_ms &&
        s_cfg.departure_buzzer_enabled == enabled) {
        xSemaphoreGive(s_config_mutex);
        return ESP_OK;
    }
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) err = nvs_set_u32(h, "departure_ms", pulse_ms);
    if (err == ESP_OK) err = nvs_set_u8(h, "departure_beep", enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(h);
    if (h) nvs_close(h);
    if (err == ESP_OK) {
        s_cfg.departure_buzzer_pulse_ms = pulse_ms;
        s_cfg.departure_buzzer_enabled = enabled;
    }
    xSemaphoreGive(s_config_mutex);
    return err;
}
