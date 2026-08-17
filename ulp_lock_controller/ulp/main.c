#include <stdint.h>
#include "ulp_riscv.h"
#include "ulp_riscv_utils.h"
#include "ulp_riscv_gpio.h"
#include "ulp_riscv_adc_ulp_core.h"
#include "hal/adc_ll.h"
#include "shared_state.h"

volatile uint32_t sequence = 0;
volatile int32_t baseline_raw = -1;
volatile int32_t vbat_raw = -1;
volatile int32_t sense_raw = -1;
volatile int32_t delta_raw = -1;
volatile uint32_t normalized_feature = 0;
volatile uint32_t plate_state = PLATE_UNKNOWN;
volatile uint32_t fault_flags = 0;
volatile uint32_t measurement_counter = 0;
volatile uint32_t pulse_actual_us = 0;
volatile uint32_t command_sequence = 0;
volatile uint32_t command = ULP_CMD_UNLOCK;
volatile uint32_t command_ack_sequence = 0;
volatile uint32_t lock_state = LOCK_STATE_STARTING;
volatile uint32_t event_sequence = 0;
volatile uint32_t event_flags = ULP_EVENT_NONE;
volatile uint32_t fsm_state = LOCK_FSM_UNLOCKED_SCAN;
volatile uint32_t coil_current_raw = 0;
volatile uint32_t close_verify_state = PLATE_UNKNOWN;
volatile uint32_t close_verify_feature = 0;
volatile int32_t close_verify_delta_raw = -1;
volatile uint32_t close_attempt_counter = 0;
volatile uint32_t close_reject_counter = 0;

static uint32_t last_state = PLATE_UNKNOWN;
static uint8_t gpio_ready = 0;
static uint8_t coil_on = 0;
static uint32_t accepted_command_sequence = 0;
static uint32_t active_command = ULP_CMD_UNLOCK;
static uint8_t close_verify_mode = 0;

static int32_t adc_read_channel(int channel)
{
    adc_oneshot_ll_set_atten(ADC_UNIT_1, channel, ADC_ATTEN_DB_12);
    return ulp_riscv_adc_read_channel(ADC_UNIT_1, channel);
}

static int32_t adc_average(int channel, uint32_t count)
{
    int32_t sum = 0;
    for (uint32_t i = 0; i < count; i++) {
        const int32_t raw = adc_read_channel(channel);
        if (raw < 0) return -1;
        sum += raw;
    }
    return sum / (int32_t)count;
}

static void gate_low(void)
{
    ulp_riscv_gpio_output_level(GPIO_NUM_8, 0);
    coil_on = 0;
}

static void gate_high(void)
{
    ulp_riscv_gpio_output_level(GPIO_NUM_8, 1);
    coil_on = 1;
}

static void setup_gate(void)
{
    if (gpio_ready) return;
    ulp_riscv_gpio_init(GPIO_NUM_8);
    ulp_riscv_gpio_output_enable(GPIO_NUM_8);
    ulp_riscv_gpio_set_output_mode(GPIO_NUM_8, RTCIO_MODE_OUTPUT);
    gate_low();
    gpio_ready = 1;
}

static uint32_t classify_feature(uint32_t feature)
{
    if (last_state == PLATE_FAR) {
        if (feature < ULP_FEATURE_FAR_OFF) last_state = PLATE_GAP_5;
        return last_state;
    }
    if (feature >= ULP_FEATURE_FAR_ON) last_state = PLATE_FAR;
    else if (feature <= ULP_FEATURE_CONTACT_MAX) last_state = PLATE_CONTACT;
    else if (feature <= ULP_FEATURE_GAP1_MAX) last_state = PLATE_GAP_1;
    else if (feature <= ULP_FEATURE_GAP2_MAX) last_state = PLATE_GAP_2;
    else if (feature <= ULP_FEATURE_GAP3_MAX) last_state = PLATE_GAP_3;
    else if (feature <= ULP_FEATURE_GAP4_MAX) last_state = PLATE_GAP_4;
    else last_state = PLATE_GAP_5;
    return last_state;
}

static void raise_event(uint32_t event)
{
    event_flags |= event;
    event_sequence++;
}

static void publish_status(uint32_t state, uint32_t faults)
{
    sequence++;
    plate_state = PLATE_UNKNOWN;
    fault_flags = faults;
    lock_state = state;
    command_ack_sequence = accepted_command_sequence;
    measurement_counter++;
    sequence++;
}

static uint32_t read_command(void)
{
    const uint32_t before = command_sequence;
    const uint32_t requested = command;
    const uint32_t after = command_sequence;
    if (before == after && !(before & 1u) && before != accepted_command_sequence) {
        accepted_command_sequence = before;
        active_command = requested;
        command_ack_sequence = before;
        if (requested == ULP_CMD_UNLOCK && lock_state == LOCK_STATE_FAULT) {
            fault_flags = 0;
            lock_state = LOCK_STATE_UNLOCKED;
            fsm_state = LOCK_FSM_UNLOCKED_SCAN;
        }
    }
    return active_command;
}

static uint32_t measure_plate(void)
{
    gate_low();
    fsm_state = LOCK_FSM_UNLOCKED_SCAN;
    // During normal scans use the full discharge guard. Immediately after a
    // 300 ms close-hold, however, that guard lets the armature release before
    // verification. The close path uses a short, explicit off-window instead.
    const uint32_t off_guard_us = close_verify_mode
        ? ULP_CLOSE_VERIFY_OFF_GUARD_US : ULP_OFF_GUARD_US;
    ulp_riscv_delay_cycles(off_guard_us * ULP_RISCV_CYCLES_PER_US);

    int32_t local_baseline = adc_average(ADC_SENSE_CHANNEL, ULP_BASELINE_SAMPLES);
    ulp_riscv_delay_cycles(ULP_ADC_SETTLE_US * ULP_RISCV_CYCLES_PER_US);
    (void)adc_read_channel(ADC_VBAT_CHANNEL);
    int32_t local_vbat = adc_average(ADC_VBAT_CHANNEL, ULP_VBAT_SAMPLES);
    uint32_t faults = 0;
    if (local_baseline < 0) faults |= ULP_FAULT_ADC_INVALID;
    if (local_vbat < 100) faults |= ULP_FAULT_VBAT_INVALID;

    ulp_riscv_delay_cycles(ULP_ADC_SETTLE_US * ULP_RISCV_CYCLES_PER_US);
    if (adc_read_channel(ADC_SENSE_CHANNEL) < 0) faults |= ULP_FAULT_SENSE_INVALID;

    int32_t local_sense = -1;
    int32_t local_delta = -1;
    uint32_t feature = 0;
    uint32_t state = PLATE_UNKNOWN;
    if (faults == 0) {
        gate_high();
        ulp_riscv_delay_cycles(ULP_PULSE_US * ULP_RISCV_CYCLES_PER_US);
        local_sense = adc_read_channel(ADC_SENSE_CHANNEL);
        gate_low();
        if (local_sense < 0) {
            faults |= ULP_FAULT_SENSE_INVALID;
        } else {
            local_delta = local_sense - local_baseline;
            if (local_delta < 0) local_delta = 0;
            if ((uint32_t)local_delta < ULP_DIAG_MIN_DELTA_RAW) {
                faults |= ULP_FAULT_COIL_OPEN;
            } else {
                feature = ((uint32_t)local_delta * ULP_FEATURE_SCALE) /
                          (uint32_t)local_vbat;
                state = classify_feature(feature);
            }
        }
    }

    if (state > PLATE_UNKNOWN) {
        faults |= ULP_FAULT_STATE_INVALID;
        state = PLATE_UNKNOWN;
    }

    sequence++;
    baseline_raw = local_baseline;
    vbat_raw = local_vbat;
    sense_raw = local_sense;
    delta_raw = local_delta;
    normalized_feature = feature;
    plate_state = state;
    fault_flags = faults;
    lock_state = faults ? LOCK_STATE_FAULT : LOCK_STATE_UNLOCKED;
    fsm_state = faults ? LOCK_FSM_FAULT : LOCK_FSM_UNLOCKED_SCAN;
    measurement_counter++;
    pulse_actual_us = ULP_PULSE_US;
    command_ack_sequence = accepted_command_sequence;
    sequence++;

    if (faults) {
        raise_event(ULP_EVENT_COIL_FAULT);
        return PLATE_UNKNOWN;
    }
    return state;
}

static void enter_fault(uint32_t fault, uint32_t event)
{
    gate_low();
    fault_flags |= fault;
    lock_state = LOCK_STATE_FAULT;
    fsm_state = LOCK_FSM_FAULT;
    publish_status(LOCK_STATE_FAULT, fault_flags);
    raise_event(event);
}

static void close_after_plate(uint32_t detected_state)
{
    close_attempt_counter++;
    fsm_state = LOCK_FSM_CLOSE_ENERGIZE;
    gate_high();
    ulp_riscv_delay_cycles(ULP_CLOSE_ENERGIZE_US * ULP_RISCV_CYCLES_PER_US);

    // Do not run the normal diagnostic pulse here. It necessarily turns the
    // holding current off, and the armature can release before the sample.
    // The plate state was already measured immediately before energizing;
    // verify the 300 ms hold by measuring the continuous coil current instead.
    fsm_state = LOCK_FSM_CLOSE_VERIFY;
    ulp_riscv_delay_cycles(ULP_ADC_SETTLE_US * ULP_RISCV_CYCLES_PER_US);
    close_verify_state = detected_state;
    close_verify_feature = normalized_feature;
    close_verify_delta_raw = delta_raw;
    if (detected_state == PLATE_CONTACT || detected_state == PLATE_GAP_1) {
        fsm_state = LOCK_FSM_LOCKED;
        lock_state = LOCK_STATE_LOCKED;
        raise_event(ULP_EVENT_LOCKED);
    } else {
        close_reject_counter++;
        gate_low();
        fsm_state = LOCK_FSM_UNLOCKED_SCAN;
        lock_state = LOCK_STATE_UNLOCKED;
        raise_event(ULP_EVENT_CLOSE_REJECTED);
    }
}

static void open_from_locked(void)
{
    fsm_state = LOCK_FSM_OPEN_DISCHARGE;
    gate_low();
    ulp_riscv_delay_cycles(ULP_OFF_GUARD_US * ULP_RISCV_CYCLES_PER_US);
    fsm_state = LOCK_FSM_OPEN_VERIFY_CURRENT_OFF;
    const int32_t residual = adc_average(ADC_SENSE_CHANNEL, ULP_BASELINE_SAMPLES);
    coil_current_raw = residual < 0 ? 0 : (uint32_t)residual;
    if (residual < 0 || (uint32_t)residual > ULP_COIL_OFF_MAX_RAW) {
        enter_fault(ULP_FAULT_COIL_SHORT, ULP_EVENT_OPEN_FAILED);
        return;
    }
    lock_state = LOCK_STATE_UNLOCKED;
    fsm_state = LOCK_FSM_UNLOCKED_SCAN;
    raise_event(ULP_EVENT_UNLOCKED);
    (void)measure_plate();
}

int main(void)
{
    setup_gate();
    const uint32_t requested_command = read_command();

    if (requested_command == ULP_CMD_LOCK) {
        // A fault is latched. Do not restart the diagnostic loop while the
        // mailbox still contains LOCK: repeated fault events can flood the
        // ULP software interrupt and starve the main CPU. OPEN explicitly
        // clears/re-arms the controller through read_command().
        if (lock_state == LOCK_STATE_FAULT) {
            gate_low();
            fsm_state = LOCK_FSM_FAULT;
            return 0;
        }
        if (lock_state != LOCK_STATE_LOCKED) {
            gate_low();
            fsm_state = LOCK_FSM_CLOSE_WAIT_PLATE;
            const uint32_t state = measure_plate();
            if (state == PLATE_CONTACT || state == PLATE_GAP_1) {
                close_after_plate(state);
            }
        } else {
            fsm_state = LOCK_FSM_LOCKED;
        }
        return 0;
    }

    if (requested_command != ULP_CMD_UNLOCK) {
        enter_fault(ULP_FAULT_STATE_INVALID, ULP_EVENT_OPEN_FAILED);
        return 0;
    }

    if (lock_state == LOCK_STATE_LOCKED || fsm_state == LOCK_FSM_LOCKED) {
        open_from_locked();
    } else if (lock_state != LOCK_STATE_FAULT) {
        gate_low();
        (void)measure_plate();
    }
    return 0;
}
