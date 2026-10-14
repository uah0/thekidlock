#include "lock_control.h"

#include "app_config.h"
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
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
static atomic_bool s_web_test_active;
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
#define WEB_TEST_LEASE_MS       6000LL

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
    LOCK_EVENT_WEB_TEST_ON,
    LOCK_EVENT_WEB_TEST_OFF,
    LOCK_EVENT_TOGGLE_HOLD_OPEN,
    LOCK_EVENT_SET_HOLD_OPEN,
    LOCK_EVENT_SET_AUTO,
    LOCK_EVENT_DISABLE_AUTO_CLOSE,
    LOCK_EVENT_CLOSE,
    LOCK_EVENT_RSSI_HIGH,
    LOCK_EVENT_DISCONNECTED,
    LOCK_EVENT_INTERNET_OPEN,
} lock_event_t;

typedef struct {
    lock_event_t kind;
    char source[12], actor[18];
    uint64_t cloud_id;
    int64_t deadline_us;
} lock_event_item_t;
static char s_source[12] = "system", s_actor[18];
static uint64_t s_cloud_id;
static bool s_mode_persisted = true;
static atomic_bool s_internet,s_internet_pending;
static uint8_t s_internet_phase;
static int64_t s_internet_started_ms, s_internet_opened_ms, s_internet_started_us;
bool lock_control_internet_active(void) { return atomic_load(&s_internet)||atomic_load(&s_internet_pending); }


static esp_err_t request_attributed(const char *operation, const char *source, const char *actor,
                                    uint64_t id, int64_t deadline_us)
{
    lock_event_item_t item = {.cloud_id = id, .deadline_us = deadline_us};
    if (!operation) return ESP_ERR_INVALID_ARG;
    if (lock_control_internet_active()) return ESP_ERR_INVALID_STATE;
    if (!strcmp(operation, "open")) item.kind = LOCK_EVENT_OPEN;
    else if (!strcmp(operation, "manual_open")) item.kind = LOCK_EVENT_TEST_OPEN;
    else if (!strcmp(operation, "close")) item.kind = LOCK_EVENT_CLOSE;
    else if (!strcmp(operation, "hold_on")) item.kind = LOCK_EVENT_SET_HOLD_OPEN;
    else if (!strcmp(operation, "hold_off")) item.kind = LOCK_EVENT_SET_AUTO;
    else return ESP_ERR_INVALID_ARG;
    snprintf(item.source, sizeof(item.source), "%s", source ? source : "local");
    snprintf(item.actor, sizeof(item.actor), "%s", actor ? actor : "");
    return s_events && xQueueSend(s_events, &item, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t lock_request_attributed(const char *operation, const char *source, const char *actor)
{
    return request_attributed(operation, source, actor, 0, 0);
}

esp_err_t lock_request_cloud(const char *operation, const char *actor, uint64_t id, int64_t deadline_us)
{
    if(!operation || strcmp(operation,"internet_open") || !s_events || atomic_load(&s_internet)) return ESP_ERR_INVALID_STATE;
    bool expected=false;
    if(!atomic_compare_exchange_strong(&s_internet_pending,&expected,true)) return ESP_ERR_INVALID_STATE;
    lock_event_item_t item={.kind=LOCK_EVENT_INTERNET_OPEN,.cloud_id=id,.deadline_us=deadline_us};
    snprintf(item.source,sizeof(item.source),"cloud");
    snprintf(item.actor,sizeof(item.actor),"%s",actor?actor:"");
    if(xQueueSendToFront(s_events,&item,0)!=pdTRUE) {
        lock_event_item_t obsolete;
        xQueueReceive(s_events,&obsolete,0);
        if(xQueueSendToFront(s_events,&item,0)!=pdTRUE) {
            atomic_store(&s_internet_pending,false);return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

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
    bool web_test_active = false;
    int64_t web_test_started_ms = 0;
    int64_t web_test_deadline_ms = 0;
    bool hold_open = s_hold_open;
    bool open_armed = false;
    bool beacon_auto_cycle = false;
    bool beacon_door_open_confirmed = false;
    int64_t beacon_reopen_block_until_ms = 0;

    for (;;) {
        lock_event_item_t item;
        bool received = xQueueReceive(s_events, &item, pdMS_TO_TICKS(100)) == pdTRUE;
        lock_event_t event = received ? item.kind : LOCK_EVENT_DISCONNECTED;
        int64_t now_ms = esp_timer_get_time() / 1000;
        app_config_t cfg;
        app_config_get(&cfg);

        if (received) {
            if (item.deadline_us && esp_timer_get_time() >= item.deadline_us) {
                if(event==LOCK_EVENT_INTERNET_OPEN) atomic_store(&s_internet_pending,false);
                received = false;
            }
        }
        if (received && lock_control_internet_active() && event!=LOCK_EVENT_INTERNET_OPEN) received = false;
        if (received) {
            if (item.cloud_id) s_cloud_id = item.cloud_id;
            if (event != LOCK_EVENT_RSSI_HIGH && event != LOCK_EVENT_DISCONNECTED) {
                snprintf(s_source, sizeof(s_source), "%s", item.source[0] ? item.source : "local");
                snprintf(s_actor, sizeof(s_actor), "%s", item.actor);
                char operation[24];
                snprintf(operation, sizeof(operation), "operation=%u", (unsigned)event);
            }
            switch (event) {
                case LOCK_EVENT_INTERNET_OPEN: {
                    lock_feedback_t feedback;
                    lock_control_get_feedback(&feedback);
                    int64_t request_us = esp_timer_get_time();
                    bool current_valid = feedback.valid && feedback.sampled_at_us > 0 &&
                        request_us >= feedback.sampled_at_us &&
                        request_us - feedback.sampled_at_us <= 300000;
                    bool can_open = current_valid && !feedback.fault_flags &&
                        !feedback.calibrating && !feedback.calibration_required;
                    if (!current_valid) {
                        ulp_diagnostics_t fresh;
                        current_valid = ulp_controller_read_diagnostics(&fresh);
                        can_open = current_valid && !fresh.fault_flags &&
                            !fresh.calibration_mode && fresh.calibration_valid;
                    }
                    s_internet_phase=5; s_internet_started_us=esp_timer_get_time();
                    xQueueReset(s_events);
                    if(!can_open) { atomic_store(&s_internet_pending,false); break; }
                    s_mode_persisted=save_hold_open(false)==ESP_OK;
                    if(!s_mode_persisted) { atomic_store(&s_internet_pending,false); break; }
                    hold_open=false; s_hold_open=false; open_armed=false; test_open_active=false; web_test_active=false;
                    if(apply_state(true)!=ESP_OK) { atomic_store(&s_internet_pending,false); break; }
                    s_internet_started_ms=now_ms; s_internet_opened_ms=0;
                    s_internet_phase=1; atomic_store(&s_internet,true); atomic_store(&s_internet_pending,false);
                    break;
                }
                case LOCK_EVENT_OPEN:
                    if (!strcmp(item.source, "ble") && now_ms < beacon_reopen_block_until_ms) {
                        event_log_add("Замок: BLE-открытие отклонено защитным таймером");
                    } else if (!lock_control_requested_open()) {
                        if (apply_state(true) == ESP_OK) {
                            last_sufficient_ms = now_ms;
                            test_open_active = false;
                            web_test_active = false;
                            if (!strcmp(item.source, "ble") && !hold_open) {
                                if (!beacon_auto_cycle) beacon_door_open_confirmed = false;
                                beacon_auto_cycle = true;
                            } else {
                                beacon_auto_cycle = false;
                                beacon_door_open_confirmed = false;
                            }
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
                case LOCK_EVENT_WEB_TEST_ON: {
                    if (web_test_active) {
                        web_test_deadline_ms = now_ms + WEB_TEST_LEASE_MS;
                        break;
                    }
                    lock_feedback_t feedback;
                    lock_control_get_feedback(&feedback);
                    if (hold_open || !feedback.valid || feedback.fault_flags ||
                        feedback.calibrating || feedback.calibration_required ||
                        feedback.state == LOCK_STATE_FAULT ||
                        now_ms * 1000 - feedback.sampled_at_us > 300000) break;
                    if (apply_state(true) == ESP_OK) {
                        web_test_active = true;
                        test_open_active = false;
                        beacon_auto_cycle = false;
                        beacon_door_open_confirmed = false;
                        web_test_started_ms = now_ms;
                        web_test_deadline_ms = now_ms + WEB_TEST_LEASE_MS;
                        event_log_add("Замок: фиксируемый тест открытия включён");
                    }
                    break;
                }
                case LOCK_EVENT_WEB_TEST_OFF:
                    if (web_test_active && apply_state(false) == ESP_OK) {
                        web_test_active = false;
                        event_log_add("Замок: фиксируемый тест открытия выключен");
                    }
                    break;
                case LOCK_EVENT_TOGGLE_HOLD_OPEN:
                case LOCK_EVENT_SET_HOLD_OPEN:
                case LOCK_EVENT_SET_AUTO: {
                    bool was_hold_open = hold_open;
                    test_open_active = false;
                    web_test_active = false;
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
                    s_mode_persisted = save_hold_open(hold_open) == ESP_OK;
                    if (!s_mode_persisted) {
                        event_log_add("Замок: ошибка сохранения режима в NVS");
                    }
                    break;
                }
                case LOCK_EVENT_DISABLE_AUTO_CLOSE:
                    test_open_active = false;
                    web_test_active = false;
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
                    web_test_active = false;
                    open_armed = false;
                    if (!hold_open && lock_control_requested_open() && apply_state(false) == ESP_OK) {
                        event_log_add("Замок: запрос закрытия по единому интерфейсу");
                    }
                    break;
                case LOCK_EVENT_RSSI_HIGH:
                    if (now_ms < beacon_reopen_block_until_ms) {
                        break;
                    }
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

        if (web_test_active && now_ms >= web_test_deadline_ms) {
            if (apply_state(false) == ESP_OK) {
                web_test_active = false;
                event_log_add("Замок: фиксируемый тест завершён по таймауту связи");
            }
        }
        atomic_store(&s_web_test_active, web_test_active);
        if (lock_control_requested_open() && !hold_open && !web_test_active && !lock_control_internet_active()) {
            bool close_a = !test_open_active && cfg.timer_a_ms > 0 &&
                           now_ms - last_sufficient_ms > (int64_t)cfg.timer_a_ms;
            bool close_test = test_open_active &&
                              now_ms >= test_open_deadline_ms;
            if (close_a || close_test) {
                snprintf(s_source, sizeof(s_source), "%s", "timer");
                s_actor[0] = '\0';
                if (apply_state(false) == ESP_OK) {
                    test_open_active = false;
                    event_log_add("Замок: запрос закрытия по %s",
                                  close_a ? "таймеру А" :
                                  "тестовому таймеру");
                }
            }
        }

        refresh_feedback(now_ms);
        if (web_test_active) {
            lock_feedback_t feedback;
            lock_control_get_feedback(&feedback);
            if (feedback.valid && (feedback.fault_flags ||
                feedback.state == LOCK_STATE_FAULT ||
                feedback.calibrating || feedback.calibration_required)) {
                web_test_active = false;
                atomic_store(&s_web_test_active, false);
            } else if (feedback.valid && feedback.state == LOCK_STATE_LOCKED &&
                       now_ms - web_test_started_ms >= 2000 &&
                       apply_state(false) == ESP_OK) {
                web_test_active = false;
                atomic_store(&s_web_test_active, false);
                event_log_add("Замок: фиксируемый тест не достиг открытия за 2 секунды");
            }
        }
        if (beacon_auto_cycle) {
            lock_feedback_t feedback;
            lock_control_get_feedback(&feedback);
            if (feedback.valid && feedback.plate_state <= PLATE_FAR) {
                if (feedback.plate_state == PLATE_FAR) {
                    beacon_door_open_confirmed = true;
                } else if (feedback.plate_state == PLATE_CONTACT && beacon_door_open_confirmed) {
                    if (apply_state(false) == ESP_OK) {
                        beacon_reopen_block_until_ms = now_ms + cfg.beacon_reopen_protection_ms;
                        event_log_add("Замок: дверь закрылась после BLE-автооткрытия; повторное BLE-открытие заблокировано на %lu мс",
                                      (unsigned long)cfg.beacon_reopen_protection_ms);
                    } else {
                        event_log_add("Замок: ошибка закрытия после перехода двери в закрытое состояние");
                    }
                    beacon_auto_cycle = false;
                    beacon_door_open_confirmed = false;
                }
            }
        }
        if(atomic_load(&s_internet)) {
            lock_feedback_t f; lock_control_get_feedback(&f);
            if(f.valid && (f.fault_flags || f.state==LOCK_STATE_FAULT)) {
                apply_state(false); s_internet_phase=5; atomic_store(&s_internet,false);
            } else if(s_internet_phase==1) {
                if(f.valid && f.state==LOCK_STATE_UNLOCKED && !f.hold_open) {
                    s_internet_opened_ms=now_ms; s_internet_phase=2;
                } else if(now_ms-s_internet_started_ms>=10000) {
                    apply_state(false); s_internet_phase=5; atomic_store(&s_internet,false);
                }
            } else if(s_internet_phase==2) {
                ulp_diagnostics_t d;
                bool removed=ulp_controller_read_diagnostics(&d) && d.plate_state==PLATE_FAR;
                if(removed || now_ms-s_internet_opened_ms>=INTERNET_OPEN_AUTO_CLOSE_MS) {
                    if(apply_state(false)==ESP_OK) {
                        s_internet_phase=3;
                    }
                }
            } else if(s_internet_phase==3 && f.valid && f.state==LOCK_STATE_LOCKED) {
                s_internet_phase=4; atomic_store(&s_internet,false);
            }
            refresh_feedback(now_ms);
        }
    }
}

esp_err_t lock_control_init(void)
{
    if (s_initialized) return ESP_OK;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    s_events = xQueueCreate(8, sizeof(lock_event_item_t));
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
    if (lock_control_internet_active()) return ESP_ERR_INVALID_STATE;
    lock_event_item_t event = {.kind = LOCK_EVENT_OPEN};
    return s_events && xQueueSend(s_events, &event, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t lock_request_arm_open(void)
{
    if (lock_control_internet_active()) return ESP_ERR_INVALID_STATE;
    lock_event_item_t event = {.kind = LOCK_EVENT_ARM_OPEN};
    return s_events && xQueueSend(s_events, &event, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t lock_request_close(void)
{
    if (lock_control_internet_active()) return ESP_ERR_INVALID_STATE;
    lock_event_item_t event = {.kind = LOCK_EVENT_CLOSE};
    return s_events && xQueueSend(s_events, &event, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t lock_request_test_open(void)
{
    if (lock_control_internet_active()) return ESP_ERR_INVALID_STATE;
    lock_event_item_t event = {.kind = LOCK_EVENT_TEST_OPEN};
    return s_events && xQueueSend(s_events, &event, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t lock_request_web_test_open(bool enabled)
{
    if (lock_control_internet_active()) return ESP_ERR_INVALID_STATE;
    lock_event_item_t event = {.kind = enabled ? LOCK_EVENT_WEB_TEST_ON : LOCK_EVENT_WEB_TEST_OFF};
    return s_events && xQueueSend(s_events, &event, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool lock_control_web_test_active(void)
{
    lock_feedback_t feedback;
    lock_control_get_feedback(&feedback);
    return atomic_load(&s_web_test_active) && feedback.valid && !feedback.fault_flags &&
        !feedback.calibrating && !feedback.calibration_required &&
        feedback.state == LOCK_STATE_UNLOCKED && feedback.requested_open;
}

esp_err_t lock_request_manual_open(void)
{
    lock_event_item_t event = {.kind = LOCK_EVENT_TEST_OPEN};
    return s_events && xQueueSend(s_events, &event, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t lock_request_toggle_hold_open(void)
{
    if (lock_control_internet_active()) return ESP_ERR_INVALID_STATE;
    lock_event_item_t event = {.kind = LOCK_EVENT_TOGGLE_HOLD_OPEN};
    return s_events && xQueueSend(s_events, &event, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t lock_request_set_hold_open(bool enabled)
{
    if (lock_control_internet_active()) return ESP_ERR_INVALID_STATE;
    lock_event_item_t event = {.kind = enabled ? LOCK_EVENT_SET_HOLD_OPEN : LOCK_EVENT_SET_AUTO};
    return s_events && xQueueSend(s_events, &event, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t lock_request_disable_auto_and_close(void)
{
    if (lock_control_internet_active()) return ESP_ERR_INVALID_STATE;
    lock_event_item_t event = {.kind = LOCK_EVENT_DISABLE_AUTO_CLOSE};
    return s_events && xQueueSend(s_events, &event, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t lock_control_config_changed(void)
{
    if (!s_initialized || lock_control_internet_active()) return ESP_ERR_INVALID_STATE;
    if (lock_control_is_hold_open()) return apply_command(ULP_CMD_HOLD_OPEN);
    return apply_state(lock_control_requested_open());
}

void lock_control_report_rssi(bool sufficient)
{
    if (!s_events || !sufficient) return;
    lock_event_item_t event = {.kind = LOCK_EVENT_RSSI_HIGH};
    xQueueSend(s_events, &event, 0);
}

void lock_control_report_disconnected(void)
{
    if (!s_events) return;
    lock_event_item_t event = {.kind = LOCK_EVENT_DISCONNECTED};
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
        s_feedback.plate_state = d.plate_state;
        s_feedback.fault_flags = d.fault_flags;
        s_feedback.close_rejected = d.close_reject_latched != 0;
        s_feedback.calibrating = d.calibration_mode != 0;
        s_feedback.calibration_required = !d.calibration_valid;
        s_feedback.valid = true;
        s_feedback.sampled_at_us = now_ms * 1000;
        s_feedback_time_ms = now_ms;
    } else if (now_ms - s_feedback_time_ms > 1000) {
        s_feedback.valid = false;
        s_feedback.state = LOCK_STATE_STARTING;
    }
    s_feedback.hold_open = s_hold_open;
    s_feedback.requested_open = s_requested_open;
    s_feedback.cloud_command_id = s_cloud_id;
    s_feedback.internet_phase = s_internet_phase;
    s_feedback.internet_started_us=s_internet_started_us;
    s_feedback.internet_close_remaining_ms = 0;
    if (s_internet_phase == 2 && s_internet_opened_ms > 0) {
        int64_t elapsed = now_ms - s_internet_opened_ms;
        if (elapsed < INTERNET_OPEN_AUTO_CLOSE_MS) {
            s_feedback.internet_close_remaining_ms = (uint32_t)(INTERNET_OPEN_AUTO_CLOSE_MS - elapsed);
        }
    }
    s_feedback.mode_persisted = s_mode_persisted;
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
