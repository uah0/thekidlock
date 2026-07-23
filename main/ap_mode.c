#include "ap_mode.h"
#include "app_config.h"
#include "event_log.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

static ap_mode_callback_t s_callback;
static bool s_configuration;
static const char *TAG = "ap_mode";

static void button_task(void *arg)
{
    (void)arg;
    bool previous = gpio_get_level(APP_CONFIG_BUTTON_GPIO) == 0;
    TickType_t pressed_at = 0;
    bool long_action_fired = false;
    if (previous) pressed_at = xTaskGetTickCount();
    ESP_LOGI(TAG, "GPIO%d button task started, initial level=%d (%s)",
             APP_CONFIG_BUTTON_GPIO, previous ? 0 : 1,
             previous ? "pressed" : "released");
    event_log_add("GPIO%d: кнопка готова, начальный уровень %d",
                  APP_CONFIG_BUTTON_GPIO, previous ? 0 : 1);
    for (;;) {
        int level = gpio_get_level(APP_CONFIG_BUTTON_GPIO);
        bool pressed = level == 0;
        if (pressed && !previous) {
            pressed_at = xTaskGetTickCount();
            long_action_fired = false;
            ESP_LOGI(TAG, "GPIO%d pressed, level=0", APP_CONFIG_BUTTON_GPIO);
            event_log_add("GPIO%d: нажатие, уровень 0", APP_CONFIG_BUTTON_GPIO);
        }
        if (pressed && !long_action_fired &&
            xTaskGetTickCount() - pressed_at >= pdMS_TO_TICKS(3000)) {
            long_action_fired = true;
            if (s_configuration) {
                ESP_LOGI(TAG, "GPIO%d held 3000 ms, action=erase bonds",
                         APP_CONFIG_BUTTON_GPIO);
                event_log_add("GPIO%d: удержание 3000 мс, удалить bonds",
                              APP_CONFIG_BUTTON_GPIO);
                if (s_callback) s_callback(false, true);
            } else {
                ESP_LOGW(TAG, "GPIO%d held 3000 ms outside configuration mode; ignored",
                         APP_CONFIG_BUTTON_GPIO);
                event_log_add("GPIO%d: сброс bonds отклонён вне режима настройки",
                              APP_CONFIG_BUTTON_GPIO);
            }
        }
        if (!pressed && previous) {
            TickType_t duration = xTaskGetTickCount() - pressed_at;
            uint32_t duration_ms = (uint32_t)(duration * portTICK_PERIOD_MS);
            if (long_action_fired) {
                ESP_LOGI(TAG, "GPIO%d released after long action, duration=%lu ms, no action",
                         APP_CONFIG_BUTTON_GPIO, (unsigned long)duration_ms);
                event_log_add("GPIO%d: отпускание после удержания, без действия",
                              APP_CONFIG_BUTTON_GPIO);
            } else {
                ESP_LOGI(TAG, "GPIO%d released, level=1, duration=%lu ms, action=toggle configuration",
                         APP_CONFIG_BUTTON_GPIO, (unsigned long)duration_ms);
                event_log_add("GPIO%d: короткое нажатие %lu мс, переключить режим",
                              APP_CONFIG_BUTTON_GPIO, (unsigned long)duration_ms);
                if (s_callback) s_callback(!s_configuration, false);
            }
        }
        previous = pressed;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t ap_mode_init(ap_mode_callback_t callback)
{
    s_callback = callback;
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << APP_CONFIG_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) return err;
    /* The callback starts/stops Wi-Fi and the HTTP server in this task's
     * context. ESP-IDF logging and those driver calls need substantially more
     * stack than a GPIO-only polling loop. */
    return xTaskCreate(button_task, "ap_button", 6144, NULL, 5, NULL) == pdPASS
               ? ESP_OK : ESP_ERR_NO_MEM;
}

bool ap_mode_is_configuration(void) { return s_configuration; }
void ap_mode_set_configuration(bool enabled) { s_configuration = enabled; }
