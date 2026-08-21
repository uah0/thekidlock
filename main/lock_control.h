#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t lock_control_init(void);
esp_err_t lock_request_open(void);
esp_err_t lock_request_manual_open(void);
esp_err_t lock_request_arm_open(void);
esp_err_t lock_request_test_open(void);
esp_err_t lock_request_toggle_hold_open(void);
esp_err_t lock_request_set_hold_open(bool enabled);
esp_err_t lock_request_close(void);
esp_err_t lock_control_config_changed(void);
esp_err_t lock_control_set_gpio(int gpio_num);
void lock_control_report_rssi(bool sufficient);
void lock_control_report_disconnected(void);
bool lock_control_is_open(void);
bool lock_control_is_hold_open(void);
bool lock_control_is_blocked(void);
