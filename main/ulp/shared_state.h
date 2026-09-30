#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "experiment_config.h"

// Existing hardware mapping; these are RTC-capable on ESP32-S3.
#define LOCK_GATE_GPIO       9
#define ADC_SENSE_CHANNEL_DEFAULT 6  // GPIO7 -> ADC1_CH6
#define ADC_VBAT_CHANNEL     4  // GPIO5 -> ADC1_CH4

#define ULP_MEASURE_PERIOD_US ULP_EXPERIMENT_PERIOD_US
#define ULP_OFF_GUARD_US      ULP_EXPERIMENT_OFF_GUARD_US
#define ULP_PULSE_US          ULP_EXPERIMENT_PULSE_US
#define ULP_BASELINE_SAMPLES  8
#define ULP_VBAT_SAMPLES      8
#define ULP_ADC_SETTLE_US     100U
#define ULP_CLOSE_VERIFY_OFF_GUARD_US 100000U
#define ULP_CLOSE_ENERGIZE_US 300000U
#define ULP_GATE_TEST_DURATION_US 5000000U
#define ULP_GATE_TEST_TICKS \
    ((ULP_GATE_TEST_DURATION_US + ULP_MEASURE_PERIOD_US - 1U) / ULP_MEASURE_PERIOD_US)
#define ULP_COIL_OFF_MAX_RAW 80U
#define ULP_DIAG_MIN_DELTA_RAW 40U
#define ULP_FAULT_RETRY_LIMIT 3U
#define ULP_FAULT_RETRY_DELAY_US 5000000U
#define ULP_CALIBRATION_TIMEOUT_US 120000000LL

static inline bool ulp_calibration_values_valid(int32_t contact, int32_t paper1,
                                                int32_t paper2, int32_t open,
                                                int32_t verify)
{
    return contact >= 0 && paper1 > contact && paper2 > paper1 &&
           open > paper2 && open <= 4095 && verify >= 0 && verify <= 4095;
}

/* Temporary hardware validation only: GPIO5 now measures the Li-ion cell,
 * not the regulated 12 V coil rail. While enabled, battery ADC data remains
 * visible but cannot block or rescale a close decision. */
#define ULP_TEST_IGNORE_VBAT_FOR_LOCK 0

/* GPIO7 calibration anchors. Feature values are reconstructed from four
 * cold raw delta points without using the battery measurement on GPIO5. */
#define ULP_CAL_CONTACT_FEATURE 6000U
#define ULP_CAL_PAPER1_FEATURE  15000U
#define ULP_CAL_PAPER2_FEATURE  23000U
#define ULP_CAL_OPEN_FEATURE    52000U
#define ULP_CONTACT_BOUNDARY_PERCENT 10U
// Final hot verification is safety-critical: permanent holding current is
// allowed only very close to the calibrated zero-gap hot reference.

// Temporary internal validation: ULP simulates an open coil while LOCKED.
// Set to 0 after the fault path and LED alarm are confirmed.

// Normal mode: feature = max(sense_raw - baseline_raw, 0) * scale / vbat_raw.
// Test mode above publishes delta_raw as feature and accepts a valid coil response.
#define ULP_FEATURE_SCALE     100000U

// Initial raw-ratio thresholds mapped from the working 2 ms current bands.
// They are intentionally kept in one contract so validation can tune them.
#define ULP_FEATURE_CONTACT_MAX \
    (ULP_CAL_CONTACT_FEATURE + \
     (ULP_CAL_PAPER1_FEATURE - ULP_CAL_CONTACT_FEATURE) * \
     ULP_CONTACT_BOUNDARY_PERCENT / 100U)
#define ULP_FEATURE_GAP1_MAX    19500U
#define ULP_FEATURE_GAP2_MAX    26500U
#define ULP_FEATURE_GAP3_MAX    35000U
#define ULP_FEATURE_GAP4_MAX    42000U
#define ULP_FEATURE_GAP5_MAX    48000U
#define ULP_FEATURE_FAR_ON      48000U
#define ULP_FEATURE_FAR_OFF     45200U  // approximately 3 mA hysteresis

typedef enum {
    PLATE_CONTACT = 0,
    PLATE_GAP_1 = 1,
    PLATE_GAP_2 = 2,
    PLATE_GAP_3 = 3,
    PLATE_GAP_4 = 4,
    PLATE_GAP_5 = 5,
    PLATE_FAR = 6,
    PLATE_UNKNOWN = 7,
} plate_state_t;

typedef enum {
    ULP_CMD_UNLOCK = 0,
    ULP_CMD_LOCK = 1,
    ULP_CMD_HOLD_OPEN = 2,
    ULP_CMD_GATE_TEST = 3,
} ulp_command_t;

#define ULP_CMD_OPEN  ULP_CMD_UNLOCK
#define ULP_CMD_CLOSE ULP_CMD_LOCK

typedef enum {
    LOCK_STATE_STARTING = 0,
    LOCK_STATE_UNLOCKED = 1,
    LOCK_STATE_LOCKED = 2,
    LOCK_STATE_FAULT = 3,
} lock_state_t;

typedef enum {
    LOCK_FSM_UNLOCKED_SCAN = 0,
    LOCK_FSM_CLOSE_WAIT_PLATE = 1,
    LOCK_FSM_CLOSE_ENERGIZE = 2,
    LOCK_FSM_CLOSE_VERIFY = 3,
    LOCK_FSM_LOCKED = 4,
    LOCK_FSM_OPEN_DISCHARGE = 5,
    LOCK_FSM_OPEN_VERIFY_CURRENT_OFF = 6,
    LOCK_FSM_FAULT = 7,
    LOCK_FSM_OPEN_HOLD = 8,
    LOCK_FSM_GATE_TEST = 9,
} lock_fsm_state_t;

enum {
    ULP_FAULT_ADC_INVALID  = 1u << 0,
    ULP_FAULT_VBAT_INVALID = 1u << 1,
    ULP_FAULT_SENSE_INVALID = 1u << 2,
    ULP_FAULT_STATE_INVALID = 1u << 3,
    ULP_FAULT_COIL_OPEN = 1u << 4,
    ULP_FAULT_COIL_SHORT = 1u << 5,
};

enum {
    ULP_EVENT_NONE = 0,
    ULP_EVENT_LOCKED = 1u << 0,
    ULP_EVENT_UNLOCKED = 1u << 1,
    ULP_EVENT_COIL_FAULT = 1u << 2,
    ULP_EVENT_CLOSE_REJECTED = 1u << 3,
    ULP_EVENT_OPEN_FAILED = 1u << 4,
    ULP_EVENT_FAULT_RETRY = 1u << 5,
};

// Exported as individual volatile symbols by ulp/main.c and generated into
// ulp_main.h for the main CPU. sequence is a seqlock: odd while writing,
// even when the sample is complete.
extern volatile uint32_t sequence;
extern volatile int32_t baseline_raw;
extern volatile int32_t vbat_raw;
extern volatile int32_t sense_raw;
extern volatile uint32_t sense_adc_channel;
extern volatile int32_t delta_raw;
extern volatile uint32_t normalized_feature;
extern volatile uint32_t plate_state;
extern volatile uint32_t fault_flags;
extern volatile uint32_t measurement_counter;
extern volatile uint32_t pulse_actual_us;

// Command mailbox. The main CPU writes command first and increments
// command_sequence last. ULP acknowledges the accepted sequence.
extern volatile uint32_t command_sequence;
extern volatile uint32_t command;
extern volatile uint32_t command_ack_sequence;
extern volatile uint32_t lock_state;
extern volatile uint32_t event_sequence;
extern volatile uint32_t event_flags;
extern volatile uint32_t fsm_state;
extern volatile uint32_t coil_current_raw;
// Software-observed GPIO9 state and real HIGH/LOW transition counters.
extern volatile uint32_t gate_level;
extern volatile uint32_t gate_high_counter;
extern volatile uint32_t gate_low_counter;
// Last close verification result. These fields are diagnostic only and make
// a rejected close distinguishable from a real LOCKED state.
extern volatile uint32_t close_verify_state;
extern volatile uint32_t close_detected_state;
extern volatile uint32_t close_verify_feature;
extern volatile int32_t close_verify_delta_raw;
extern volatile uint32_t close_attempt_counter;
extern volatile uint32_t close_reject_counter;
extern volatile uint32_t close_reject_latched;
extern volatile uint32_t calibration_mode;
extern volatile uint32_t fault_retry_count;
extern volatile int32_t calibration_contact_raw;
extern volatile int32_t calibration_paper1_raw;
extern volatile int32_t calibration_paper2_raw;
extern volatile int32_t calibration_open_raw;
extern volatile int32_t calibration_verify_contact_raw;
extern volatile uint32_t calibration_verify_request;
extern volatile uint32_t calibration_verify_counter;
extern volatile int32_t calibration_verify_raw;
