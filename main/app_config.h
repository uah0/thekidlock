#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "app_build_number.h"

#define APP_VERSION                      "10.0." APP_BUILD_NUMBER_STRING
#define APP_CONFIG_BUTTON_GPIO          0
#define APP_BUZZER_GPIO                  8
#define APP_BUZZER_ACTIVE_LEVEL          0
#define APP_RGB_LED_GPIO                 48

#define APP_DEFAULT_PHONE_RSSI_THRESHOLD (-68)
#define APP_DEFAULT_BLE_TX_POWER_DBM         3
#define APP_DEFAULT_TIMER_A_MS           3000
#define APP_DEFAULT_SHUNT_GPIO           7
#define APP_RSSI_THRESHOLD_MIN           (-127)
#define APP_RSSI_THRESHOLD_MAX           0
#define APP_BLE_TX_POWER_MIN_DBM         (-12)
#define APP_BLE_TX_POWER_MAX_DBM         9
#define APP_TIMER_S_MAX                  3600
#define APP_TIMER_A_MS_MAX               600000

typedef struct {
    int8_t phone_rssi_threshold;      /* единственный порог зоны открытия/ухода */
    int8_t ble_tx_power_dbm;               /* мощность передатчика BLE, dBm */
    uint8_t shunt_gpio;
    uint32_t timer_a_ms;
    bool configuration_mode;
} app_config_t;

esp_err_t app_config_init(void);
void app_config_get(app_config_t *out);
esp_err_t app_config_set_logic(int phone_rssi_threshold,
                               uint32_t timer_a_ms);
esp_err_t app_config_set_ble_tx_power(int power_dbm);
bool app_config_shunt_gpio_valid(uint32_t gpio);
esp_err_t app_config_set_shunt_gpio(uint8_t gpio);
