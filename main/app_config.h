#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "app_build_number.h"

#define APP_VERSION                      "10.0." APP_BUILD_NUMBER_STRING
#define APP_CONFIG_BUTTON_GPIO          0
#define APP_BUZZER_GPIO                  8
#define APP_BUZZER_ACTIVE_LEVEL          0
#define APP_DEFAULT_DEPARTURE_PULSE_MS   20
#define APP_DEPARTURE_PULSE_MIN_MS       0
#define APP_DEPARTURE_PULSE_MAX_MS       120
#define APP_DEPARTURE_PULSE_STEP_MS      1
#define APP_RGB_LED_GPIO                 48

#define APP_DEFAULT_PHONE_RSSI_THRESHOLD (-68)
#define APP_BLE_TX_POWER_DBM               9
#define APP_DEFAULT_TIMER_A_MS           3000
#define APP_DEFAULT_SHUNT_GPIO           6
#define APP_RSSI_THRESHOLD_MIN           (-127)
#define APP_RSSI_THRESHOLD_MAX           0
#define APP_TIMER_S_MAX                  3600
#define APP_TIMER_A_MS_MAX               600000
#define APP_DEFAULT_BLE_REOPEN_PROTECTION_MS 3000
#define APP_BLE_REOPEN_PROTECTION_MS_MIN  3000
#define APP_BLE_REOPEN_PROTECTION_MS_MAX  8000
#define APP_BLE_REOPEN_PROTECTION_MS_STEP 100

typedef struct {
    int8_t phone_rssi_threshold;      /* единственный порог зоны открытия/ухода */
    uint8_t shunt_gpio;
    bool serial_log_enabled;
    bool departure_buzzer_enabled;
    uint32_t departure_buzzer_pulse_ms;
    uint32_t timer_a_ms;
    uint32_t beacon_reopen_protection_ms;
    bool configuration_mode;
} app_config_t;

esp_err_t app_config_init(void);
void app_config_get(app_config_t *out);
esp_err_t app_config_set_logic(int phone_rssi_threshold,
                               uint32_t timer_a_ms,
                               uint32_t beacon_reopen_protection_ms);
esp_err_t app_config_set_serial_log_enabled(bool enabled);
esp_err_t app_config_set_departure_buzzer_enabled(bool enabled);

esp_err_t app_config_set_departure_buzzer_pulse_ms(uint32_t pulse_ms);
