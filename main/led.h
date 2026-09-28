#pragma once

#include <stdbool.h>
#include "esp_err.h"

/* RGB-светодиод светится НЕПРЕРЫВНО текущим цветом, соответствующим
 * состоянию замка/системы, и никак не реагирует на бипер/клики
 * led_set_state() переключает базовый цвет мгновенно и без мигания;
 * вызывается из lock_control, web_ui.c и main.c. */
typedef enum {
    LED_STATE_BOOT = 0,      /* инициализация, Wi-Fi/BLE ещё не готовы */
    LED_STATE_LOCKED,        /* замок заперт (LOCKED) */
    LED_STATE_UNLOCKED,      /* замок открыт (UNLOCKED) */
    LED_STATE_LOCKED_BLOCKED,/* жёлтый: повторное открытие временно заблокировано */
    LED_STATE_OTA,           /* идёт запись прошивки, скоро перезагрузка */
    LED_STATE_CONFIGURATION, /* активен режим настройки */
    LED_STATE_FAULT,         /* пурпурный: ULP fault или отклонённое закрытие */
} led_state_t;

esp_err_t led_init(void);
void led_set_state(led_state_t state);
void led_set_configuration(bool enabled);
esp_err_t led_flash_bond_reset(void);
