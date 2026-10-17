#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "shared_state.h"

/* The ULP owns the solenoid gate and ADC pins after initialization. */
esp_err_t ulp_controller_init(void);
/* Serialized last-command-wins mailbox. ESP_OK means queued, not actuated. */
esp_err_t ulp_controller_request(ulp_command_t command);
/* Bounded acknowledgement wait for HTTP direct commands, not physical completion. */
esp_err_t ulp_controller_request_wait(ulp_command_t command);
bool ulp_controller_read_state(lock_state_t *state, uint32_t *faults,
                               uint32_t *events);

typedef struct {
    lock_state_t lock_state;
    lock_fsm_state_t fsm_state;
    plate_state_t plate_state;
    uint32_t fault_flags;
    uint32_t event_flags;
    int32_t baseline_raw;
    int32_t vbat_raw;
    int32_t sense_raw;
    int32_t delta_raw;
    uint32_t normalized_feature;
    uint32_t coil_current_raw;
    uint32_t gate_level;
    uint32_t gate_high_counter;
    uint32_t gate_low_counter;
    uint32_t requested_command;
    uint32_t calibration_mode;
    bool calibration_valid;
    uint32_t calibration_timeout_count;
    uint32_t fault_retry_count;
    uint32_t calibration_verify_counter;
    int32_t calibration_verify_raw;
    int32_t calibration_contact_raw;
    int32_t calibration_paper1_raw;
    int32_t calibration_paper2_raw;
    int32_t calibration_open_raw;
    int32_t calibration_verify_contact_raw;
    uint32_t measurement_counter;
    uint32_t pulse_actual_us;
    uint32_t close_verify_state;
    uint32_t close_detected_state;
    uint32_t close_verify_feature;
    int32_t close_verify_delta_raw;
    uint32_t close_attempt_counter;
    uint32_t close_reject_counter;
    uint32_t close_reject_latched;
    uint32_t command_ack_sequence;
    uint32_t command_sequence;
    uint32_t command_overlap_counter;
    uint32_t event_sequence;
} ulp_diagnostics_t;

typedef struct {
    uint64_t cursor;
    uint32_t pulse_sequence;
    uint32_t measurement_counter;
    uint32_t pulse_actual_us;
    int32_t baseline_raw, vbat_raw, sense_raw, delta_raw;
    uint32_t normalized_feature;
    uint32_t plate_state, fault_flags, lock_state, fsm_state;
    uint32_t event_flags, requested_command;
    uint32_t source_dropped_before;
} ulp_pulse_record_t;

bool ulp_controller_read_diagnostics(ulp_diagnostics_t *out);
/* Copies each newly published ULP pulse into a dedicated RAM ring. */
bool ulp_controller_capture_latest_pulse(void);
size_t ulp_controller_read_pulse_records(uint64_t since, char *out, size_t capacity,
    uint64_t *next, uint64_t *oldest, bool *dropped);
esp_err_t ulp_controller_calibration_start(void);
void ulp_controller_calibration_cancel(void);
esp_err_t ulp_controller_calibration_verify(void);
esp_err_t ulp_controller_calibration_save(int32_t closed_raw,
                                          int32_t open_raw,
                                          int32_t verify_contact_raw);
