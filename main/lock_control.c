#include "lock_control.h"

#include "app_config.h"
#include "event_log.h"
#include "led.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static SemaphoreHandle_t s_mutex;
static bool s_initialized;
static bool s_is_open;
static bool s_reopen_blocked;
static QueueHandle_t s_events;
static int s_gpio = APP_LOCK_GPIO;

typedef enum {
    LOCK_EVENT_OPEN,
    LOCK_EVENT_ARM_OPEN,
    LOCK_EVENT_TEST_OPEN,
    LOCK_EVENT_CLOSE,
    LOCK_EVENT_RSSI_HIGH,
    LOCK_EVENT_RSSI_LOW,
    LOCK_EVENT_DISCONNECTED,
} lock_event_t;

static esp_err_t apply_state(bool open)
{
    if (!s_initialized || !s_mutex) return ESP_ERR_INVALID_STATE;

    app_config_t cfg;
    app_config_get(&cfg);
    int level = open ? cfg.lock_open_level : !cfg.lock_open_level;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = gpio_set_level((gpio_num_t)s_gpio, level);
    if (err == ESP_OK) {
        s_is_open = open;
    }
    xSemaphoreGive(s_mutex);
    return err;
}

static void publish_reopen_blocked(bool blocked)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_reopen_blocked = blocked;
    xSemaphoreGive(s_mutex);
}

static void lock_task(void *arg)
{
    (void)arg;
    int64_t last_sufficient_ms = 0;
    int64_t long_open_deadline_ms = 0;
    bool reopen_blocked = false;
    bool open_armed = false;
    uint8_t low_rssi_count = 0;
    bool release_pending = false;
    bool release_condition_active = false;
    bool connection_present = false;
    int64_t release_deadline_ms = 0;

    for (;;) {
        lock_event_t event;
        bool received = xQueueReceive(s_events, &event, pdMS_TO_TICKS(100)) == pdTRUE;
        int64_t now_ms = esp_timer_get_time() / 1000;
        app_config_t cfg;
        app_config_get(&cfg);

        if (received) {
            switch (event) {
                case LOCK_EVENT_OPEN:
                    open_armed = true;
                    if (!reopen_blocked && !lock_control_is_open()) {
                        if (apply_state(true) == ESP_OK) {
                            last_sufficient_ms = now_ms;
                            long_open_deadline_ms = now_ms + cfg.long_open_ms;
                            led_set_state(LED_STATE_UNLOCKED);
                            event_log_add("Замок UNLOCKED по единому интерфейсу");
                        }
                    }
                    break;
                case LOCK_EVENT_ARM_OPEN:
                    open_armed = true;
                    event_log_add("Замок: открытие вооружено, ожидание RSSI");
                    break;
                case LOCK_EVENT_TEST_OPEN:
                    // Admin-initiated test open is a deliberate manual override: it must
                    // always physically actuate the lock, even while automatic BLE/RSSI
                    // reopening is being held back by the anti-flap "reopen_blocked" guard.
                    // It intentionally does not touch open_armed, so it doesn't start
                    // arming automatic RSSI-triggered reopen behaviour.
                    if (!lock_control_is_open()) {
                        if (apply_state(true) == ESP_OK) {
                            last_sufficient_ms = now_ms;
                            long_open_deadline_ms = now_ms + cfg.long_open_ms;
                            led_set_state(LED_STATE_UNLOCKED);
                            event_log_add("Замок UNLOCKED по тестовой команде (админ, минуя блокировку)");
                        }
                    }
                    break;
                case LOCK_EVENT_CLOSE:
                    open_armed = false;
                    if (lock_control_is_open() && apply_state(false) == ESP_OK) {
                        led_set_state(LED_STATE_LOCKED);
                        event_log_add("Замок LOCKED по единому интерфейсу");
                    }
                    break;
                case LOCK_EVENT_RSSI_HIGH:
                    connection_present = true;
                    if (lock_control_is_open()) {
                        last_sufficient_ms = now_ms;
                    } else if (open_armed && !reopen_blocked) {
                        if (apply_state(true) == ESP_OK) {
                            last_sufficient_ms = now_ms;
                            long_open_deadline_ms = now_ms + cfg.long_open_ms;
                            led_set_state(LED_STATE_UNLOCKED);
                            event_log_add("Замок UNLOCKED при повторном приближении телефона");
                        }
                    }
                    // Strict consecutive streak: any single strong sample resets the
                    // low-RSSI counter to zero. Note the trade-off: RSSI hovering right
                    // at the threshold can keep the lock permanently reopen_blocked,
                    // since one "sufficient" sample every few ticks prevents the streak
                    // from ever reaching the release count. Requested explicitly despite
                    // this trade-off.
                    if (reopen_blocked) {
                        low_rssi_count = 0;
                        release_condition_active = false;
                        if (release_pending) {
                            release_pending = false;
                            event_log_add("Замок: таймер Б отменён, RSSI восстановился");
                        }
                    }
                    break;
                case LOCK_EVENT_RSSI_LOW:
                    connection_present = true;
                    release_condition_active = true;
                    if (reopen_blocked && !release_pending && low_rssi_count < UINT8_MAX) {
                        low_rssi_count++;
                        if (low_rssi_count >= 3) {
                            low_rssi_count = 0;
                            if (cfg.timer_b_s == 0) {
                                reopen_blocked = false;
                                publish_reopen_blocked(false);
                                if (!lock_control_is_open()) led_set_state(LED_STATE_LOCKED);
                                event_log_add("Замок: запрет повторного открытия снят по слабому RSSI");
                            } else {
                                release_pending = true;
                                release_deadline_ms = now_ms + (int64_t)cfg.timer_b_s * 1000;
                                event_log_add("Замок: таймер Б запущен на %lu с по слабому RSSI",
                                              (unsigned long)cfg.timer_b_s);
                            }
                        }
                    }
                    break;
                case LOCK_EVENT_DISCONNECTED:
                    open_armed = false;
                    connection_present = false;
                    release_condition_active = true;
                    if (reopen_blocked) {
                        low_rssi_count = 0;
                        if (cfg.timer_b_s == 0) {
                            reopen_blocked = false;
                            release_pending = false;
                            publish_reopen_blocked(false);
                            if (!lock_control_is_open()) led_set_state(LED_STATE_LOCKED);
                            event_log_add("Замок: запрет повторного открытия снят по потере BLE-соединения");
                        } else if (!release_pending) {
                            release_pending = true;
                            release_deadline_ms = now_ms + (int64_t)cfg.timer_b_s * 1000;
                            event_log_add("Замок: таймер Б запущен на %lu с по потере BLE-соединения",
                                          (unsigned long)cfg.timer_b_s);
                        }
                    }
                    break;
            }
        }

        if (lock_control_is_open()) {
            bool close_a = cfg.timer_a_ms > 0 &&
                           now_ms - last_sufficient_ms > (int64_t)cfg.timer_a_ms;
            bool close_long = now_ms >= long_open_deadline_ms;
            if (close_a || close_long) {
                if (apply_state(false) == ESP_OK) {
                    if (!close_a && close_long) {
                        reopen_blocked = true;
                        low_rssi_count = 0;
                        release_pending = false;
                        release_condition_active = false;
                        publish_reopen_blocked(true);
                    }
                    led_set_state(reopen_blocked ? LED_STATE_LOCKED_BLOCKED : LED_STATE_LOCKED);
                    event_log_add("Замок LOCKED по %s",
                                  close_a ? "таймеру А" : "таймеру долго открыт");
                }
            }
        }

        if (reopen_blocked && release_pending && now_ms >= release_deadline_ms) {
            release_pending = false;
            low_rssi_count = 0;
            if (release_condition_active || !connection_present) {
                reopen_blocked = false;
                publish_reopen_blocked(false);
                if (!lock_control_is_open()) led_set_state(LED_STATE_LOCKED);
                event_log_add("Замок: таймер Б истёк, условие подтверждено, включён красный");
            } else {
                event_log_add("Замок: таймер Б истёк, условие перехода не подтверждено");
            }
        }
    }
}

esp_err_t lock_control_init(void)
{
    if (s_initialized) return ESP_OK;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    s_events = xQueueCreate(8, sizeof(lock_event_t));
    if (!s_events) return ESP_ERR_NO_MEM;

    app_config_t cfg;
    app_config_get(&cfg);
    int closed_level = !cfg.lock_open_level;
    s_gpio = cfg.lock_gpio;

    /* Preload the safe (energized/locked) level before enabling the output. */
    gpio_set_level((gpio_num_t)s_gpio, closed_level);
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << s_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;
    err = gpio_set_level((gpio_num_t)s_gpio, closed_level);
    if (err != ESP_OK) return err;

    s_is_open = false;
    s_initialized = true;

    /* The lock always boots physically LOCKED (safe/closed level applied
     * above), so the LED must reflect that immediately rather than sitting
     * on the neutral boot color until the first open/close event. Requires
     * led_init() to already have run before lock_control_init(). */
    led_set_state(LED_STATE_LOCKED);

    return xTaskCreate(lock_task, "lock_control", 4096, NULL, 6, NULL) == pdPASS
               ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t lock_control_set_gpio(int gpio_num)
{
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio_num)) return ESP_ERR_INVALID_ARG;
    if (!s_initialized || !s_mutex) return ESP_ERR_INVALID_STATE;
    if (gpio_num == s_gpio) return ESP_OK;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    app_config_t cfg;
    app_config_get(&cfg);
    int level = s_is_open ? cfg.lock_open_level : !cfg.lock_open_level;

    /* Release the old pin safely, then move the same logical level to the
     * new pin so the physical lock state doesn't change mid-switch. */
    gpio_set_level((gpio_num_t)s_gpio, !cfg.lock_open_level);
    gpio_reset_pin((gpio_num_t)s_gpio);

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err == ESP_OK) {
        s_gpio = gpio_num;
        err = gpio_set_level((gpio_num_t)s_gpio, level);
    }

    xSemaphoreGive(s_mutex);
    return err;
}

esp_err_t lock_request_open(void)
{
    lock_event_t event = LOCK_EVENT_OPEN;
    return s_events && xQueueSend(s_events, &event, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t lock_request_arm_open(void)
{
    lock_event_t event = LOCK_EVENT_ARM_OPEN;
    return s_events && xQueueSend(s_events, &event, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t lock_request_close(void)
{
    lock_event_t event = LOCK_EVENT_CLOSE;
    return s_events && xQueueSend(s_events, &event, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t lock_request_test_open(void)
{
    lock_event_t event = LOCK_EVENT_TEST_OPEN;
    return s_events && xQueueSend(s_events, &event, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t lock_control_config_changed(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    return apply_state(lock_control_is_open());
}

void lock_control_report_rssi(bool sufficient)
{
    if (!s_events) return;
    lock_event_t event = sufficient ? LOCK_EVENT_RSSI_HIGH : LOCK_EVENT_RSSI_LOW;
    xQueueSend(s_events, &event, 0);
}

void lock_control_report_disconnected(void)
{
    if (!s_events) return;
    lock_event_t event = LOCK_EVENT_DISCONNECTED;
    xQueueSend(s_events, &event, 0);
}

bool lock_control_is_open(void)
{
    if (!s_initialized || !s_mutex) return false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool open = s_is_open;
    xSemaphoreGive(s_mutex);
    return open;
}

bool lock_control_is_blocked(void)
{
    if (!s_initialized || !s_mutex) return false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool blocked = s_reopen_blocked;
    xSemaphoreGive(s_mutex);
    return blocked;
}
