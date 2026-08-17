#pragma once

#include <stdint.h>
#include "experiment_config.h"

// Existing hardware mapping; these are RTC-capable on ESP32-S3.
#define LOCK_GATE_GPIO       8
#define ADC_SENSE_CHANNEL    6  // GPIO7 -> ADC1_CH6
#define ADC_VBAT_CHANNEL     4  // GPIO5 -> ADC1_CH4

#define ULP_MEASURE_PERIOD_US ULP_EXPERIMENT_PERIOD_US
#define ULP_OFF_GUARD_US      ULP_EXPERIMENT_OFF_GUARD_US
#define ULP_PULSE_US          ULP_EXPERIMENT_PULSE_US
#define ULP_BASELINE_SAMPLES  8
#define ULP_VBAT_SAMPLES      8
#define ULP_ADC_SETTLE_US     100U
#define ULP_CLOSE_VERIFY_OFF_GUARD_US 500U
#define ULP_CLOSE_ENERGIZE_US 300000U
#define ULP_COIL_OFF_MAX_RAW 80U
#define ULP_DIAG_MIN_DELTA_RAW 40U

// Temporary internal validation: ULP simulates an open coil while LOCKED.
// Set to 0 after the fault path and LED alarm are confirmed.

// Fixed point feature = max(sense_raw - baseline_raw, 0) * scale / vbat_raw.
#define ULP_FEATURE_SCALE     100000U

// Initial raw-ratio thresholds mapped from the working 2 ms current bands.
// They are intentionally kept in one contract so validation can tune them.
#define ULP_FEATURE_CONTACT_MAX 12000U
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
};

// Exported as individual volatile symbols by ulp/main.c and generated into
// ulp_main.h for the main CPU. sequence is a seqlock: odd while writing,
// even when the sample is complete.
extern volatile uint32_t sequence;
extern volatile int32_t baseline_raw;
extern volatile int32_t vbat_raw;
extern volatile int32_t sense_raw;
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
// Last close verification result. These fields are diagnostic only and make
// a rejected close distinguishable from a real LOCKED state.
extern volatile uint32_t close_verify_state;
extern volatile uint32_t close_verify_feature;
extern volatile int32_t close_verify_delta_raw;
extern volatile uint32_t close_attempt_counter;
extern volatile uint32_t close_reject_counter;
