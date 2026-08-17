#include "app_config.h"

#include <string.h>
#include "driver/gpio.h"
#include "nvs.h"

#define NVS_NS "blelock"

static app_config_t s_cfg;

static void set_defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.phone_rssi_threshold = APP_DEFAULT_PHONE_RSSI_THRESHOLD;
    s_cfg.phone_rssi_hold_threshold =
        (int8_t)(APP_DEFAULT_PHONE_RSSI_THRESHOLD - APP_DEFAULT_PHONE_RSSI_HOLD_MARGIN);
    s_cfg.long_open_ms = APP_DEFAULT_LONG_OPEN_MS;
    s_cfg.timer_a_ms = APP_DEFAULT_TIMER_A_MS;
    s_cfg.timer_b_s = APP_DEFAULT_TIMER_B_S;
    s_cfg.lock_gpio = APP_LOCK_GPIO;
    s_cfg.lock_open_level = APP_LOCK_DEFAULT_OPEN_LEVEL;
    s_cfg.configuration_mode = false;
}

static bool values_valid(int phone_rssi, int phone_rssi_hold, uint32_t long_open_ms,
                         uint32_t timer_a_ms, uint32_t timer_b_s, int lock_gpio)
{
    return phone_rssi >= APP_RSSI_THRESHOLD_MIN &&
           phone_rssi <= APP_RSSI_THRESHOLD_MAX &&
           phone_rssi_hold >= APP_RSSI_THRESHOLD_MIN &&
           phone_rssi_hold <= phone_rssi && /* слабее или равен строгому порогу открытия */
           long_open_ms >= APP_LONG_OPEN_MS_MIN &&
           long_open_ms <= APP_TIMER_MS_MAX &&
           timer_a_ms <= APP_TIMER_A_MS_MAX &&
           timer_b_s <= APP_TIMER_B_S_MAX &&
           (timer_a_ms == 0 || long_open_ms > timer_a_ms) &&
           lock_gpio == APP_LOCK_GPIO;
}

esp_err_t app_config_init(void)
{
    set_defaults();
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;

    int8_t phone_rssi = s_cfg.phone_rssi_threshold;
    int8_t phone_rssi_hold = s_cfg.phone_rssi_hold_threshold;
    uint32_t long_open_ms = s_cfg.long_open_ms;
    uint32_t timer_a_ms = s_cfg.timer_a_ms;
    uint32_t timer_b_s = s_cfg.timer_b_s;
    uint8_t lock_open_level = s_cfg.lock_open_level;
    nvs_get_i8(h, "phone_rssi", &phone_rssi);
    nvs_get_i8(h, "phone_rssi_hold", &phone_rssi_hold);
    nvs_get_u32(h, "long_open_ms", &long_open_ms);
    nvs_get_u32(h, "timer_a_ms", &timer_a_ms);
    nvs_get_u32(h, "timer_b_s", &timer_b_s);
    nvs_get_u8(h, "lock_open", &lock_open_level);
    nvs_close(h);

    if (values_valid(phone_rssi, phone_rssi_hold, long_open_ms, timer_a_ms, timer_b_s,
                     APP_LOCK_GPIO)) {
        s_cfg.phone_rssi_threshold = phone_rssi;
        s_cfg.phone_rssi_hold_threshold = phone_rssi_hold;
        s_cfg.long_open_ms = long_open_ms;
        s_cfg.timer_a_ms = timer_a_ms;
        s_cfg.timer_b_s = timer_b_s;
        s_cfg.lock_gpio = APP_LOCK_GPIO;
        s_cfg.lock_open_level = lock_open_level != 0;
    }
    return ESP_OK;
}

void app_config_get(app_config_t *out)
{
    if (out) *out = s_cfg;
}

esp_err_t app_config_set_logic(int phone_rssi, int phone_rssi_hold, uint32_t long_open_ms,
                               uint32_t timer_a_ms, uint32_t timer_b_s, int lock_gpio,
                               bool lock_open_level)
{
    if (!values_valid(phone_rssi, phone_rssi_hold, long_open_ms, timer_a_ms, timer_b_s,
                      lock_gpio)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    if (err == ESP_OK) err = nvs_set_i8(h, "phone_rssi", phone_rssi);
    if (err == ESP_OK) err = nvs_set_i8(h, "phone_rssi_hold", phone_rssi_hold);
    if (err == ESP_OK) err = nvs_set_u32(h, "long_open_ms", long_open_ms);
    if (err == ESP_OK) err = nvs_set_u32(h, "timer_a_ms", timer_a_ms);
    if (err == ESP_OK) err = nvs_set_u32(h, "timer_b_s", timer_b_s);
    if (err == ESP_OK) err = nvs_set_i32(h, "lock_gpio", lock_gpio);
    if (err == ESP_OK) err = nvs_set_u8(h, "lock_open", lock_open_level ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    if (err == ESP_OK) {
        s_cfg.phone_rssi_threshold = phone_rssi;
        s_cfg.phone_rssi_hold_threshold = phone_rssi_hold;
        s_cfg.long_open_ms = long_open_ms;
        s_cfg.timer_a_ms = timer_a_ms;
        s_cfg.timer_b_s = timer_b_s;
        s_cfg.lock_gpio = lock_gpio;
        s_cfg.lock_open_level = lock_open_level;
    }
    return err;
}
