#include "led.h"
#include "app_config.h"

#include <stdbool.h>
#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* Цвет + яркость (0-255 на канал) для каждого непрерывного состояния -
 * подобрано по смыслу светофора: красный = заперто, зелёный = открыто,
 * жёлтый = заперто и заблокировано (ждём, пока сигнал упадёт ниже
 * порога открытия), синий = служебное (OTA), белый = нейтральная загрузка. */
typedef struct { uint8_t r, g, b; } led_color_t;

static const led_color_t COLOR_BOOT        = {40, 40, 40};   /* белый, тускло */
static const led_color_t COLOR_LOCKED      = {180, 0, 0};    /* красный */
static const led_color_t COLOR_UNLOCKED    = {0, 200, 0};    /* зелёный, ярко */
static const led_color_t COLOR_LOCKED_BLOCKED = {200, 120, 0}; /* жёлтый - заперт и заблокирован */
static const led_color_t COLOR_OTA         = {0, 90, 255};   /* синий */
static const led_color_t COLOR_CONFIGURATION = {0, 0, 200};  /* синий, режим настройки */

static SemaphoreHandle_t s_mutex;
static led_strip_handle_t s_strip;
static bool s_ready;
static bool s_configuration;
static bool s_flash_active;
static led_state_t s_state = LED_STATE_BOOT;

static void apply_color(const led_color_t *c)
{
    if (!s_ready) return;
    led_strip_set_pixel(s_strip, 0, c->r, c->g, c->b);
    led_strip_refresh(s_strip);
}

static const led_color_t *color_for_state(led_state_t state)
{
    switch (state) {
        case LED_STATE_LOCKED:      return &COLOR_LOCKED;
        case LED_STATE_UNLOCKED:    return &COLOR_UNLOCKED;
        case LED_STATE_LOCKED_BLOCKED: return &COLOR_LOCKED_BLOCKED;
        case LED_STATE_OTA:         return &COLOR_OTA;
        case LED_STATE_CONFIGURATION: return &COLOR_CONFIGURATION;
        case LED_STATE_BOOT:
        default:                    return &COLOR_BOOT;
    }
}

static void apply_display_state(void)
{
    apply_color(color_for_state(s_configuration && s_state != LED_STATE_OTA
                                    ? LED_STATE_CONFIGURATION : s_state));
}

static void bond_reset_flash_task(void *arg)
{
    (void)arg;
    const led_color_t white = {200, 200, 200};
    const led_color_t off = {0, 0, 0};
    for (int i = 0; i < 3; i++) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        apply_color(&white);
        xSemaphoreGive(s_mutex);
        vTaskDelay(pdMS_TO_TICKS(150));

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        apply_color(&off);
        xSemaphoreGive(s_mutex);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_flash_active = false;
    apply_display_state();
    xSemaphoreGive(s_mutex);
    vTaskDelete(NULL);
}

esp_err_t led_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    led_strip_config_t strip_config = {
        .strip_gpio_num = APP_RGB_LED_GPIO,
        .max_leds = 1,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags = { .invert_out = false },
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags = { .with_dma = false },
    };
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) return err;
    s_ready = true;
    apply_color(&COLOR_BOOT);

    return ESP_OK;
}

void led_set_state(led_state_t state)
{
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state = state;
    if (!s_flash_active) apply_display_state();
    xSemaphoreGive(s_mutex);
}

void led_set_configuration(bool enabled)
{
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_configuration = enabled;
    if (!s_flash_active) apply_display_state();
    xSemaphoreGive(s_mutex);
}

esp_err_t led_flash_bond_reset(void)
{
    if (!s_mutex || !s_ready) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_flash_active) {
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }
    s_flash_active = true;
    xSemaphoreGive(s_mutex);
    if (xTaskCreate(bond_reset_flash_task, "bond_reset_led", 2048,
                    NULL, 4, NULL) != pdPASS) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_flash_active = false;
        apply_display_state();
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
