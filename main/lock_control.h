#pragma once
unsigned lock_control_stack_free(void);

#include <stdbool.h>
#include "esp_err.h"
#include "shared_state.h"

typedef struct {
    lock_state_t state;
    uint32_t fault_flags;
    bool valid, close_rejected, calibrating, requested_open, hold_open;
    bool calibration_required;
    bool mode_persisted;
    int64_t sampled_at_us;
    uint64_t cloud_command_id;
} lock_feedback_t;
void lock_control_get_feedback(lock_feedback_t *out);
const char *lock_control_feedback_name(const lock_feedback_t *state);
const char *lock_control_status_name(void);

esp_err_t lock_control_init(void);
esp_err_t lock_request_open(void);
esp_err_t lock_request_arm_open(void);
esp_err_t lock_request_manual_open(void);
esp_err_t lock_request_test_open(void);
esp_err_t lock_request_toggle_hold_open(void);
esp_err_t lock_request_set_hold_open(bool enabled);
esp_err_t lock_request_disable_auto_and_close(void);
esp_err_t lock_request_close(void);
esp_err_t lock_request_attributed(const char *operation, const char *source, const char *actor);
esp_err_t lock_request_cloud(const char *operation, const char *actor, uint64_t id, int64_t deadline_us);
esp_err_t lock_control_config_changed(void);
void lock_control_report_rssi(bool sufficient);
void lock_control_report_disconnected(void);
bool lock_control_is_open(void);
bool lock_control_is_hold_open(void);
