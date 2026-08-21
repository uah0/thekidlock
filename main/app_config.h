#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "app_build_number.h"

#define APP_VERSION                      "10.0." APP_BUILD_NUMBER_STRING
#define APP_WIFI_SSID                    "Cisco"
#define APP_WIFI_PASSWORD                "123456798"
#define APP_CONFIG_AP_SSID              "lock"
#define APP_CONFIG_AP_PASSWORD          "123456798"
#define APP_CONFIG_BUTTON_GPIO          0
#define APP_LOCK_GPIO                    8
#define APP_LOCK_DEFAULT_OPEN_LEVEL      0
#define APP_RGB_LED_GPIO                 48

#define APP_DEFAULT_PHONE_RSSI_THRESHOLD (-68)
#define APP_DEFAULT_PHONE_RSSI_HOLD_MARGIN 10
#define APP_DEFAULT_BLE_CONNECT_RSSI_THRESHOLD (-85)
#define APP_DEFAULT_BLE_DISCONNECT_RSSI_THRESHOLD (-95)
#define APP_DEFAULT_BLE_TX_POWER_DBM         3
#define APP_DEFAULT_LONG_OPEN_MS         10000
#define APP_DEFAULT_TIMER_A_MS           3000
#define APP_DEFAULT_TIMER_B_S            5
#define APP_LONG_OPEN_MS_MIN             1000
#define APP_RSSI_THRESHOLD_MIN           (-127)
#define APP_RSSI_THRESHOLD_MAX           0
#define APP_BLE_TX_POWER_MIN_DBM         (-12)
#define APP_BLE_TX_POWER_MAX_DBM         9
#define APP_TIMER_MS_MAX                 3600000
#define APP_TIMER_A_MS_MAX               600000
#define APP_TIMER_B_S_MAX                3600

typedef struct {
    int8_t phone_rssi_threshold;      /* строгий порог RSSI ESP32 для допуска открытия */
    int8_t phone_rssi_hold_threshold; /* порог RSSI потери присутствия после подключения;
                                        * также удерживает уже открытый замок до таймера А. */
    int8_t ble_connect_rssi_threshold;    /* минимальный RSSI для допуска BLE-сеанса */
    int8_t ble_disconnect_rssi_threshold; /* RSSI разрыва уже установленного BLE-сеанса */
    int8_t ble_tx_power_dbm;               /* мощность передатчика BLE, dBm */
    uint32_t long_open_ms;
    uint32_t timer_a_ms;
    uint32_t timer_b_s;
    int lock_gpio;
    bool lock_open_level;
    bool configuration_mode;
} app_config_t;

esp_err_t app_config_init(void);
void app_config_get(app_config_t *out);
esp_err_t app_config_set_logic(int phone_rssi_threshold,
                               int phone_rssi_hold_threshold,
                               int ble_connect_rssi_threshold,
                               int ble_disconnect_rssi_threshold,
                               uint32_t long_open_ms,
                               uint32_t timer_a_ms,
                               uint32_t timer_b_s,
                               int lock_gpio,
                               bool lock_open_level);
esp_err_t app_config_set_ble_tx_power(int power_dbm);
