#include "lock_control.h"

#include "app_config.h"
#include "event_log.h"
#include "led.h"
#include "esp_timer.h"
#include "nvs.h"
#include "ulp_controller.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static SemaphoreHandle_t s_mutex;
static bool s_initialized;
static bool s_requested_open;
static volatile bool s_hold_open;
static QueueHandle_t s_events;
static TaskHandle_t s_lock_task;
unsigned lock_control_stack_free(void)
{
    return s_lock_task ? (unsigned)uxTaskGetStackHighWaterMark(s_lock_task) : 0;
}
static lock_feedback_t s_feedback = {.state = LOCK_STATE_STARTING};
static int64_t s_feedback_time_ms;
static bool lock_control_requested_open(void);
static void refresh_feedback(int64_t now_ms);

#define LOCK_MODE_NVS_NAMESPACE "lock_mode"
#define LOCK_MODE_NVS_KEY       "hold_open"
#define TEST_OPEN_DURATION_MS   10000LL

static bool load_hold_open(void)
{
    nvs_handle_t nvs = 0;
    uint8_t stored = 0;
    if (nvs_open(LOCK_MODE_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) return false;
    esp_err_t err = nvs_get_u8(nvs, LOCK_MODE_NVS_KEY, &stored);
    nvs_close(nvs);
    return err == ESP_OK && stored == 1;
}

static esp_err_t save_hold_open(bool enabled)
{
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(LOCK_MODE_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) err = nvs_set_u8(nvs, LOCK_MODE_NVS_KEY, enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(nvs);
    if (nvs) nvs_close(nvs);
    return err;
}

typedef enum {
    LOCK_EVENT_OPEN,
    LOCK_EVENT_ARM_OPEN,
    LOCK_EVENT_TEST_OPEN,
    LOCK_EVENT_TOGGLE_HOLD_OPEN,
    LOCK_EVENT_SET_HOLD_OPEN,
    LOCK_EVENT_SET_AUTO,
    LOCK_EVENT_DISABLE_AUTO_CLOSE,
    LOCK_EVENT_CLOSE,
    LOCK_EVENT_RSSI_HIGH,
    LOCK_EVENT_DISCONNECTED,
} lock_event_t;

static esp_err_t apply_command(ulp_command_t command)
{
    if (!s_initialized || !s_mutex) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = ulp_controller_request(command);
    if (err == ESP_OK) s_requested_open = command != ULP_CMD_CLOSE;
    xSemaphoreGive(s_mutex);
    return err;
}

static esp_err_t apply_state(bool open)
{
    return apply_command(open ? ULP_CMD_OPEN : ULP_CMD_CLOSE);
}

static void lock_task(void *arg)
{
    (void)arg;
    int64_t last_sufficient_ms = 0;
    int64_t test_open_deadline_ms = 0;
    bool test_open_active = false;
    bool hold_open = s_hold_open;
    bool open_armed = false;

    for (;;) {
        lock_event_t event;
        bool received = xQueueReceive(s_events, &event, pdMS_TO_TICKS(100)) == pdTRUE;
        int64_t now_ms = esp_timer_get_time() / 1000;
        app_config_t cfg;
        app_config_get(&cfg);

        if (received) {
            switch (event) {
                case LOCK_EVENT_OPEN:
                    if (!lock_control_requested_open()) {
                        if (apply_state(true) == ESP_OK) {
                            last_sufficient_ms = now_ms;
                            test_open_active = false;
                            event_log_add("Замок: запрос открытия по единому интерфейсу");
                        }
                    }
                    break;
                case LOCK_EVENT_ARM_OPEN:
                    open_armed = true;
                    event_log_add("Замок: открытие вооружено внешней командой, ожидание RSSI");
                    break;
                case LOCK_EVENT_TEST_OPEN:
                    // Admin-initiated test open is a deliberate manual override.
                    if (!lock_control_requested_open()) {
                        if (apply_state(true) == ESP_OK) {
                            last_sufficient_ms = now_ms;
                            test_open_deadline_ms = now_ms + TEST_OPEN_DURATION_MS;
                            test_open_active = true;
                            event_log_add("Замок: тест открытия на 10 секунд запущен (админ, минуя блокировку)");
                        }
                    }
                    break;
                case LOCK_EVENT_TOGGLE_HOLD_OPEN:
                case LOCK_EVENT_SET_HOLD_OPEN:
                case LOCK_EVENT_SET_AUTO: {
                    bool was_hold_open = hold_open;
                    test_open_active = false;
                    hold_open = event == LOCK_EVENT_TOGGLE_HOLD_OPEN ? !hold_open :
                                event == LOCK_EVENT_SET_HOLD_OPEN;
                    s_hold_open = hold_open;
                    if (hold_open) {
                        if (apply_command(ULP_CMD_HOLD_OPEN) != ESP_OK) {
                            hold_open = false;
                            s_hold_open = false;
                            event_log_add("Замок: не удалось включить режим Не закрывать");
                            break;
                        }
                        last_sufficient_ms = now_ms;
                        event_log_add("Замок: режим Не закрывать включён");
                    } else {
                        if (was_hold_open && lock_control_requested_open() &&
                            apply_state(true) != ESP_OK) {
                            event_log_add("Замок: не удалось возобновить измерение зазора");
                        }
                        last_sufficient_ms = now_ms;
                        event_log_add("Замок: режим Не закрывать выключен");
                    }
                    if (save_hold_open(hold_open) != ESP_OK) {
                        event_log_add("Замок: ошибка сохранения режима в NVS");
                    }
                    break;
                }
                case LOCK_EVENT_DISABLE_AUTO_CLOSE:
                    test_open_active = false;
                    open_armed = false;
                    hold_open = false;
                    s_hold_open = false;
                    if (save_hold_open(false) != ESP_OK) {
                        event_log_add("Замок: ошибка сохранения AUTO в NVS");
                    }
                    if (!lock_control_requested_open() || apply_state(false) == ESP_OK) {
                        event_log_add("Замок: запрос закрытия: автоматический режим выключен телефоном");
                    } else {
                        event_log_add("Замок: ошибка закрытия при выключении автоматического режима");
                    }
                    break;
                case LOCK_EVENT_CLOSE:
                    test_open_active = false;
                    open_armed = false;
                    if (!hold_open && lock_control_requested_open() && apply_state(false) == ESP_OK) {
                        event_log_add("Замок: запрос закрытия по единому интерфейсу");
                    }
                    break;
                case LOCK_EVENT_RSSI_HIGH:
                    if (lock_control_requested_open()) {
                        last_sufficient_ms = now_ms;
                    } else if (open_armed) {
                        if (apply_state(true) == ESP_OK) {
                            last_sufficient_ms = now_ms;
                            event_log_add("Замок: открытие по ранее полученной команде ARM_OPEN");
                        }
                    }
                    break;
                case LOCK_EVENT_DISCONNECTED:
                    open_armed = false;
                    break;
            }
        }

        if (lock_control_requested_open() && !hold_open) {
            bool close_a = !test_open_active && cfg.timer_a_ms > 0 &&
                           now_ms - last_sufficient_ms > (int64_t)cfg.timer_a_ms;
            bool close_test = test_open_active &&
                              now_ms >= test_open_deadline_ms;
            if (close_a || close_test) {
                if (apply_state(false) == ESP_OK) {
                    test_open_active = false;
                    event_log_add("Замок: запрос закрытия по %s",
                                  close_a ? "таймеру А" :
                                  "тестовому таймеру");
                }
            }
        }

        refresh_feedback(now_ms);
    }
}

esp_err_t lock_control_init(void)
{
    if (s_initialized) return ESP_OK;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    s_events = xQueueCreate(8, sizeof(lock_event_t));
    if (!s_events) { vSemaphoreDelete(s_mutex); s_mutex = NULL; return ESP_ERR_NO_MEM; }

    s_hold_open = load_hold_open();
    s_requested_open = s_hold_open;
    esp_err_t err = ulp_controller_request(s_hold_open ? ULP_CMD_HOLD_OPEN : ULP_CMD_CLOSE);
    if (err != ESP_OK) goto failed;
    s_initialized = true;

    /* The ULP has not confirmed either state yet. */
    led_set_state(LED_STATE_BOOT);
    event_log_add("Замок: режим из NVS — %s", s_hold_open ? "ALWAYS_OPEN" : "AUTO");

    if (xTaskCreate(lock_task, "lock_control", 4096, NULL, 6, &s_lock_task) == pdPASS) return ESP_OK;
    err = ESP_ERR_NO_MEM;
failed:
    s_initialized = false;
    vQueueDelete(s_events); s_events = NULL;
    vSemaphoreDelete(s_mutex); s_mutex = NULL;
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

esp_err_t lock_request_manual_open(void)
{
    lock_event_t event = LOCK_EVENT_TEST_OPEN;
    return s_events && xQueueSend(s_events, &event, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t lock_request_toggle_hold_open(void)
{
    lock_event_t event = LOCK_EVENT_TOGGLE_HOLD_OPEN;
    return s_events && xQueueSend(s_events, &event, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t lock_request_set_hold_open(bool enabled)
{
    lock_event_t event = enabled ? LOCK_EVENT_SET_HOLD_OPEN : LOCK_EVENT_SET_AUTO;
    return s_events && xQueueSend(s_events, &event, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t lock_request_disable_auto_and_close(void)
{
    lock_event_t event = LOCK_EVENT_DISABLE_AUTO_CLOSE;
    return s_events && xQueueSend(s_events, &event, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t lock_control_config_changed(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (lock_control_is_hold_open()) return apply_command(ULP_CMD_HOLD_OPEN);
    return apply_state(lock_control_requested_open());
}

void lock_control_report_rssi(bool sufficient)
{
    if (!s_events || !sufficient) return;
    lock_event_t event = LOCK_EVENT_RSSI_HIGH;
    xQueueSend(s_events, &event, 0);
}

void lock_control_report_disconnected(void)
{
    if (!s_events) return;
    lock_event_t event = LOCK_EVENT_DISCONNECTED;
    xQueueSend(s_events, &event, 0);
}

static bool lock_control_requested_open(void)
{
    if (!s_initialized || !s_mutex) return false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool open = s_requested_open;
    xSemaphoreGive(s_mutex);
    return open;
}

bool lock_control_is_hold_open(void)
{
    return s_initialized && s_hold_open;
}


static void refresh_feedback(int64_t now_ms)
{
    ulp_diagnostics_t d;
    bool valid = ulp_controller_read_diagnostics(&d);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    lock_feedback_t previous = s_feedback;
    if (valid) {
        s_feedback.state = d.lock_state;
        s_feedback.fault_flags = d.fault_flags;
        s_feedback.close_rejected = d.close_reject_latched != 0;
        s_feedback.calibrating = d.calibration_mode != 0;
        s_feedback.calibration_required = !d.calibration_valid;
        s_feedback.valid = true;
        s_feedback_time_ms = now_ms;
    } else if (now_ms - s_feedback_time_ms > 1000) {
        s_feedback.valid = false;
        s_feedback.state = LOCK_STATE_STARTING;
    }
    s_feedback.hold_open = s_hold_open;
    s_feedback.requested_open = s_requested_open;
    lock_feedback_t current = s_feedback;
    xSemaphoreGive(s_mutex);

    static int last_led = -1;
    led_state_t led = !current.valid ? LED_STATE_BOOT :
        current.state == LOCK_STATE_FAULT || current.fault_flags || current.close_rejected ? LED_STATE_FAULT :
        current.calibrating ? LED_STATE_CONFIGURATION :
        current.calibration_required ? LED_STATE_FAULT :
        current.state == LOCK_STATE_LOCKED ? LED_STATE_LOCKED :
        current.state == LOCK_STATE_UNLOCKED ? LED_STATE_UNLOCKED : LED_STATE_BOOT;
    if ((int)led != last_led) { led_set_state(led); last_led = led; }
    if (previous.state != current.state || previous.valid != current.valid ||
        previous.fault_flags != current.fault_flags ||
        previous.close_rejected != current.close_rejected ||
        previous.calibration_required != current.calibration_required) {
        event_log_add("ULP факт: %s fault=0x%lx", lock_control_feedback_name(&current),
                      (unsigned long)current.fault_flags);
    }
}

void lock_control_get_feedback(lock_feedback_t *out)
{
    if (!out) return;
    if (!s_initialized || !s_mutex) {
        *out = (lock_feedback_t){.state = LOCK_STATE_STARTING};
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_feedback;
    out->hold_open = s_hold_open;
    out->requested_open = s_requested_open;
    xSemaphoreGive(s_mutex);
}

const char *lock_control_feedback_name(const lock_feedback_t *state)
{
    if (!state->valid) return "STARTING";
    if (state->state == LOCK_STATE_FAULT || state->fault_flags) return "FAULT";
    if (state->calibrating) return "CALIBRATING";
    if (state->calibration_required) return "CALIBRATION_REQUIRED";
    if (state->close_rejected) return "CLOSE_REJECTED";
    if (state->state == LOCK_STATE_LOCKED) return state->hold_open ? "OPENING_HOLD" : "LOCKED";
    if (state->state == LOCK_STATE_UNLOCKED) return state->hold_open ? "OPEN_HOLD" : "OPEN";
    return "STARTING";
}

const char *lock_control_status_name(void)
{
    lock_feedback_t state;
    lock_control_get_feedback(&state);
    return lock_control_feedback_name(&state);
}

bool lock_control_is_open(void)
{
    lock_feedback_t state;
    lock_control_get_feedback(&state);
    return state.valid && state.state == LOCK_STATE_UNLOCKED && !state.fault_flags;
}
