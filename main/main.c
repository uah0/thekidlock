#include <stdio.h>
#include <string.h>
#include "ap_mode.h"
#include "app_config.h"
#include "ble_phone.h"
#include "led.h"
#include "lock_control.h"
#include "event_log.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "main";
static bool s_ble_initialized;

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static esp_err_t configuration_start(void)
{
    ble_phone_set_registration_mode(true);
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

void app_main(void)
{
    ESP_ERROR_CHECK(init_nvs());
    event_log_init();
    event_log_add("ESP32-S3 start, firmware %s", APP_VERSION);

    ESP_ERROR_CHECK(app_config_init());
    ESP_ERROR_CHECK(led_init());
    led_set_state(LED_STATE_BOOT);
    ESP_ERROR_CHECK(lock_control_init());
    ESP_ERROR_CHECK(ble_phone_init());
    s_ble_initialized = true;
    ESP_ERROR_CHECK(ap_mode_init(mode_button_callback));
    ESP_ERROR_CHECK(xTaskCreate(diagnostic_console_task, "com_diag", 6144, NULL,
                                4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    event_log_add("Нормальный режим: BLE включён, Wi-Fi и Web UI отключены");
}
