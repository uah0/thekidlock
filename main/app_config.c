#include "app_config.h"

#include <string.h>
#include "driver/gpio.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define NVS_NS "blelock"
static const char *TAG = "app_config";

static app_config_t s_cfg;
static SemaphoreHandle_t s_config_mutex;

static void set_defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.phone_rssi_threshold = APP_DEFAULT_PHONE_RSSI_THRESHOLD;
    s_cfg.ble_tx_power_dbm = APP_DEFAULT_BLE_TX_POWER_DBM;
    s_cfg.timer_a_ms = APP_DEFAULT_TIMER_A_MS;
    s_cfg.shunt_gpio = APP_DEFAULT_SHUNT_GPIO;
    s_cfg.configuration_mode = false;
}

static bool values_valid(int phone_rssi, uint32_t timer_a_ms)
{
    return phone_rssi >= APP_RSSI_THRESHOLD_MIN &&
           phone_rssi <= APP_RSSI_THRESHOLD_MAX &&
           timer_a_ms <= APP_TIMER_A_MS_MAX;
}

esp_err_t app_config_init(void)
{
    set_defaults();
    if (!s_config_mutex) s_config_mutex = xSemaphoreCreateMutex();
    if (!s_config_mutex) return ESP_ERR_NO_MEM;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;

    int8_t phone_rssi = s_cfg.phone_rssi_threshold;
    int8_t ble_tx_power_dbm = s_cfg.ble_tx_power_dbm;
    uint32_t timer_a_ms = s_cfg.timer_a_ms;
    uint8_t shunt_gpio = s_cfg.shunt_gpio;
    nvs_get_i8(h, "phone_rssi", &phone_rssi);
    nvs_get_i8(h, "ble_tx_power", &ble_tx_power_dbm);
    nvs_get_u32(h, "timer_a_ms", &timer_a_ms);
    nvs_get_u8(h, "shunt_gpio", &shunt_gpio);
    nvs_close(h);

    nvs_handle_t cleanup;
    if (nvs_open(NVS_NS, NVS_READWRITE, &cleanup) == ESP_OK) {
        esp_err_t old_long_open_s = nvs_erase_key(cleanup, "long_open_s");
        esp_err_t old_long_open_ms = nvs_erase_key(cleanup, "long_open_ms");
        if (old_long_open_s == ESP_OK || old_long_open_ms == ESP_OK) {
            esp_err_t cleanup_err = nvs_commit(cleanup);
            if (cleanup_err != ESP_OK) {
                ESP_LOGW(TAG, "Не удалось удалить устаревшую настройку long-open: %s",
                         esp_err_to_name(cleanup_err));
            }
        }
        nvs_close(cleanup);
    }

    if (values_valid(phone_rssi, timer_a_ms)) {
        s_cfg.phone_rssi_threshold = phone_rssi;
        if (ble_tx_power_dbm >= APP_BLE_TX_POWER_MIN_DBM &&
            ble_tx_power_dbm <= APP_BLE_TX_POWER_MAX_DBM &&
            (ble_tx_power_dbm - APP_BLE_TX_POWER_MIN_DBM) % 3 == 0) {
            s_cfg.ble_tx_power_dbm = ble_tx_power_dbm;
        }
        s_cfg.timer_a_ms = timer_a_ms;
    }
    if (app_config_shunt_gpio_valid(shunt_gpio)) s_cfg.shunt_gpio = shunt_gpio;
    return ESP_OK;
}

void app_config_get(app_config_t *out)
{
    if (!out) return;
    if (s_config_mutex) xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    *out = s_cfg;
    if (s_config_mutex) xSemaphoreGive(s_config_mutex);
}

static esp_err_t set_logic_locked(int phone_rssi, uint32_t timer_a_ms)
{
    if (!values_valid(phone_rssi, timer_a_ms)) {
        ESP_LOGE(TAG, "logic validation failed: zone=%d A=%lu",
                 phone_rssi, (unsigned long)timer_a_ms);
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    if (err == ESP_OK) err = nvs_set_i8(h, "phone_rssi", phone_rssi);
    if (err == ESP_OK) err = nvs_set_u32(h, "timer_a_ms", timer_a_ms);
    if (err == ESP_OK) {
        nvs_erase_key(h, "long_open_s");
        nvs_erase_key(h, "long_open_ms");
        nvs_erase_key(h, "lock_gpio");
        nvs_erase_key(h, "lock_open");
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    if (err == ESP_OK) {
        s_cfg.phone_rssi_threshold = phone_rssi;
        s_cfg.timer_a_ms = timer_a_ms;
    }
    return err;
}

static esp_err_t set_power_locked(int power_dbm)
{
    if (power_dbm < APP_BLE_TX_POWER_MIN_DBM ||
        power_dbm > APP_BLE_TX_POWER_MAX_DBM ||
        (power_dbm - APP_BLE_TX_POWER_MIN_DBM) % 3 != 0) {
        ESP_LOGE(TAG, "BLE power validation failed: %d dBm", power_dbm);
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_i8(h, "ble_tx_power", (int8_t)power_dbm);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) s_cfg.ble_tx_power_dbm = (int8_t)power_dbm;
    return err;
}

esp_err_t app_config_set_logic(int phone_rssi, uint32_t timer_a_ms)
{
    if (!s_config_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    esp_err_t err = set_logic_locked(phone_rssi, timer_a_ms);
    xSemaphoreGive(s_config_mutex);
    return err;
}
esp_err_t app_config_set_ble_tx_power(int power_dbm)
{
    if (!s_config_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    esp_err_t err = set_power_locked(power_dbm);
    xSemaphoreGive(s_config_mutex);
    return err;
}

bool app_config_shunt_gpio_valid(uint32_t gpio)
{
    return gpio == 1 || gpio == 2 || gpio == 4 || gpio == 6 ||
           gpio == 7 || gpio == 10;
}

esp_err_t app_config_set_shunt_gpio(uint8_t gpio)
{
    if (!app_config_shunt_gpio_valid(gpio)) return ESP_ERR_INVALID_ARG;
    if (!s_config_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_config_mutex, portMAX_DELAY);
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) err = nvs_set_u8(h, "shunt_gpio", gpio);
    if (err == ESP_OK) err = nvs_commit(h);
    if (h) nvs_close(h);
    if (err == ESP_OK) s_cfg.shunt_gpio = gpio;
    xSemaphoreGive(s_config_mutex);
    return err;
}
