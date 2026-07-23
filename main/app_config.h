#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define APP_VERSION                      "10.0.0"
#define APP_WIFI_SSID                    "Cisco"
#define APP_WIFI_PASSWORD                "123456798"
#define APP_CONFIG_AP_SSID              "lock"
#define APP_CONFIG_AP_PASSWORD          "123456798"
#define APP_CONFIG_BUTTON_GPIO          0
#define APP_LOCK_GPIO                    5
#define APP_LOCK_DEFAULT_OPEN_LEVEL      0
#define APP_RGB_LED_GPIO                 48

#define APP_DEFAULT_PHONE_RSSI_THRESHOLD (-68)
#define APP_DEFAULT_PHONE_RSSI_HOLD_MARGIN 10
#define APP_DEFAULT_LONG_OPEN_MS         10000
#define APP_DEFAULT_TIMER_A_MS           3000
#define APP_DEFAULT_TIMER_B_S            5
#define APP_LONG_OPEN_MS_MIN             1000
#define APP_RSSI_THRESHOLD_MIN           (-127)
#define APP_RSSI_THRESHOLD_MAX           0
#define APP_TIMER_MS_MAX                 3600000
#define APP_TIMER_A_MS_MAX               600000
#define APP_TIMER_B_S_MAX                3600

typedef struct {
    int8_t phone_rssi_threshold;      /* строгий порог: нужен, чтобы ОТКРЫТЬ замок */
    int8_t phone_rssi_hold_threshold; /* слабее (более отрицательный или равный):
                                        * достаточно, чтобы держать УЖЕ открытый замок
                                        * открытым и не давать Таймеру А начать отсчёт.
                                        * Разделение двух порогов устраняет дребезг
                                        * open/close, когда сигнал колеблется прямо
                                        * возле границы. */
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
                               uint32_t long_open_ms,
                               uint32_t timer_a_ms,
                               uint32_t timer_b_s,
                               int lock_gpio,
                               bool lock_open_level);
