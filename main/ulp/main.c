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
volatile uint32_t sense_adc_channel = ADC_SENSE_CHANNEL_DEFAULT;
volatile int32_t delta_raw = -1;
volatile uint32_t normalized_feature = 0;
volatile uint32_t plate_state = PLATE_UNKNOWN;
volatile uint32_t fault_flags = 0;
volatile uint32_t measurement_counter = 0;
volatile uint32_t pulse_actual_us = 0;
volatile uint32_t pulse_sequence = 0;
volatile uint32_t command_sequence = 0;
volatile uint32_t command = ULP_CMD_UNLOCK;
volatile uint32_t command_ack_sequence = 0;
volatile uint32_t lock_state = LOCK_STATE_STARTING;
volatile uint32_t event_sequence = 0;
volatile uint32_t event_flags = ULP_EVENT_NONE;
volatile uint32_t fsm_state = LOCK_FSM_UNLOCKED_SCAN;
volatile uint32_t coil_current_raw = 0;
volatile uint32_t gate_level = 0;
volatile uint32_t gate_high_counter = 0;
volatile uint32_t gate_low_counter = 0;
volatile uint32_t close_verify_state = PLATE_UNKNOWN;
volatile uint32_t close_detected_state = PLATE_UNKNOWN;
volatile uint32_t close_verify_feature = 0;
volatile int32_t close_verify_delta_raw = -1;
volatile uint32_t close_attempt_counter = 0;
volatile uint32_t close_reject_counter = 0;
volatile uint32_t close_reject_latched = 0;
volatile uint32_t calibration_mode = 0;
volatile uint32_t fault_retry_count = 0;
volatile int32_t calibration_contact_raw = -1;
volatile int32_t calibration_paper1_raw = -1;
volatile int32_t calibration_paper2_raw = -1;
volatile int32_t calibration_open_raw = -1;
volatile int32_t calibration_verify_contact_raw = -1;
volatile uint32_t calibration_verify_request = 0;
volatile uint32_t calibration_verify_counter = 0;
volatile int32_t calibration_verify_raw = -1;

static uint32_t last_state = PLATE_UNKNOWN;
static uint8_t gpio_ready = 0;
static uint8_t coil_on = 0;
static uint32_t accepted_command_sequence = 0;
static uint32_t active_command = ULP_CMD_UNLOCK;
static uint8_t close_verify_mode = 0;
static uint32_t fault_retry_wait_us;
static uint32_t gate_test_ticks;
static uint32_t active_sense_adc_channel = ADC_SENSE_CHANNEL_DEFAULT;

static bool sense_adc_channel_valid(uint32_t channel)
{
    return channel == 0 || channel == 1 || channel == 3 ||
           channel == 5 || channel == 6 || channel == 9;
}

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
    ulp_riscv_gpio_output_level(GPIO_NUM_9, 0);
    if (coil_on) gate_low_counter++;
    coil_on = 0;
    gate_level = 0;
}

static void gate_high(void)
{
    ulp_riscv_gpio_output_level(GPIO_NUM_9, 1);
    if (!coil_on) gate_high_counter++;
    coil_on = 1;
    gate_level = 1;
}

static void setup_gate(void)
{
    if (gpio_ready) return;
    ulp_riscv_gpio_init(GPIO_NUM_9);
    ulp_riscv_gpio_output_enable(GPIO_NUM_9);
    ulp_riscv_gpio_set_output_mode(GPIO_NUM_9, RTCIO_MODE_OUTPUT);
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

static uint32_t calibrated_feature(int32_t delta)
{
    const int32_t contact = calibration_contact_raw;
    const int32_t paper1 = calibration_paper1_raw;
    const int32_t paper2 = calibration_paper2_raw;
    const int32_t open = calibration_open_raw;
    if (!ulp_calibration_values_valid(contact, paper1, paper2, open,
                                      calibration_verify_contact_raw)) return 0;
    if (delta <= contact) return ULP_CAL_CONTACT_FEATURE;
    if (delta <= paper1) {
        return ULP_CAL_CONTACT_FEATURE +
            (uint32_t)(delta - contact) *
            (ULP_CAL_PAPER1_FEATURE - ULP_CAL_CONTACT_FEATURE) /
            (uint32_t)(paper1 - contact);
    }
    if (delta <= paper2) {
        return ULP_CAL_PAPER1_FEATURE +
            (uint32_t)(delta - paper1) *
            (ULP_CAL_PAPER2_FEATURE - ULP_CAL_PAPER1_FEATURE) /
            (uint32_t)(paper2 - paper1);
    }
    if (delta >= open) return ULP_CAL_OPEN_FEATURE;
    return ULP_CAL_PAPER2_FEATURE +
        (uint32_t)(delta - paper2) *
        (ULP_CAL_OPEN_FEATURE - ULP_CAL_PAPER2_FEATURE) /
        (uint32_t)(open - paper2);
}

static uint32_t calibrated_verify_feature(int32_t delta)
{
    if (calibration_verify_contact_raw < 0 || calibration_contact_raw < 0) return 0;
    const int32_t offset = calibration_verify_contact_raw - calibration_contact_raw;
    int32_t adjusted = delta - offset;
    if (adjusted < 0) adjusted = 0;
    return calibrated_feature(adjusted);
}

static void raise_event(uint32_t event)
{
    event_flags |= event;
    event_sequence++;
}

static void publish_status(uint32_t state, uint32_t faults)
{
    plate_state = PLATE_UNKNOWN;
    fault_flags = faults;
    lock_state = state;
    command_ack_sequence = accepted_command_sequence;
    measurement_counter++;
}

static uint32_t read_command(void)
{
    const uint32_t before = command_sequence;
    __sync_synchronize();
    const uint32_t requested = command;
    __sync_synchronize();
    const uint32_t after = command_sequence;
    if (before == after && !(before & 1u) && before != accepted_command_sequence) {
        accepted_command_sequence = before;
        active_command = requested;
        fault_retry_count = 0;
        fault_retry_wait_us = 0;
        if (requested == ULP_CMD_GATE_TEST) {
            gate_test_ticks = ULP_GATE_TEST_TICKS;
        }
        command_ack_sequence = before;
        if (requested != ULP_CMD_LOCK && lock_state == LOCK_STATE_FAULT) {
            fault_flags = 0;
            lock_state = LOCK_STATE_UNLOCKED;
            fsm_state = LOCK_FSM_UNLOCKED_SCAN;
        }
        if (requested != ULP_CMD_LOCK) close_reject_latched = 0;
    }
    return active_command;
}

static uint32_t measure_plate(void)
{
    gate_low();
    fsm_state = LOCK_FSM_UNLOCKED_SCAN;
    // During normal scans use the full discharge guard. Immediately after a
    // 300 ms close-hold the same calibrated 2 ms measurement is valid only
    // after the coil transient has decayed, so verification uses a full guard.
    const uint32_t off_guard_us = close_verify_mode
        ? ULP_CLOSE_VERIFY_OFF_GUARD_US : ULP_OFF_GUARD_US;
    ulp_riscv_delay_cycles(off_guard_us * ULP_RISCV_CYCLES_PER_US);

    int32_t local_baseline = adc_average((int)active_sense_adc_channel, ULP_BASELINE_SAMPLES);
    ulp_riscv_delay_cycles(ULP_ADC_SETTLE_US * ULP_RISCV_CYCLES_PER_US);
    (void)adc_read_channel(ADC_VBAT_CHANNEL);
    int32_t local_vbat = adc_average(ADC_VBAT_CHANNEL, ULP_VBAT_SAMPLES);
    uint32_t faults = 0;
    if (local_baseline < 0) faults |= ULP_FAULT_ADC_INVALID;

    ulp_riscv_delay_cycles(ULP_ADC_SETTLE_US * ULP_RISCV_CYCLES_PER_US);
    if (adc_read_channel((int)active_sense_adc_channel) < 0) faults |= ULP_FAULT_SENSE_INVALID;

    int32_t local_sense = -1;
    int32_t local_delta = -1;
    uint32_t feature = 0;
    uint32_t state = PLATE_UNKNOWN;
    if (faults == 0) {
        gate_high();
        ulp_riscv_delay_cycles(ULP_PULSE_US * ULP_RISCV_CYCLES_PER_US);
        local_sense = adc_read_channel((int)active_sense_adc_channel);
        gate_low();
        if (local_sense < 0) {
            faults |= ULP_FAULT_SENSE_INVALID;
        } else {
            local_delta = local_sense - local_baseline;
            if (local_delta < 0) local_delta = 0;
            if ((uint32_t)local_delta < ULP_DIAG_MIN_DELTA_RAW) {
                faults |= ULP_FAULT_COIL_OPEN;
            } else {
                if (ULP_TEST_IGNORE_VBAT_FOR_LOCK) {
                    feature = (uint32_t)local_delta;
                    state = PLATE_CONTACT;
                } else {
                    feature = calibrated_feature(local_delta);
                    state = feature == 0 ? PLATE_UNKNOWN : classify_feature(feature);
                }
            }
        }
    }

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
    pulse_sequence++;
    command_ack_sequence = accepted_command_sequence;

    if (faults) {
        raise_event(ULP_EVENT_COIL_FAULT);
        return PLATE_UNKNOWN;
    }
    return state;
}

static void measure_battery_locked(void)
{
    // GPIO9 must remain continuously HIGH while locked. Reading ADC1_CH4 does
    // not require a coil diagnostic pulse or an interruption of holding power.
    ulp_riscv_delay_cycles(ULP_ADC_SETTLE_US * ULP_RISCV_CYCLES_PER_US);
    (void)adc_read_channel(ADC_VBAT_CHANNEL);
    const int32_t local_vbat = adc_average(ADC_VBAT_CHANNEL, ULP_VBAT_SAMPLES);

    vbat_raw = local_vbat;
    measurement_counter++;
    command_ack_sequence = accepted_command_sequence;
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
    close_detected_state = detected_state;
    close_attempt_counter++;
    fsm_state = LOCK_FSM_CLOSE_ENERGIZE;
    gate_high();
    ulp_riscv_delay_cycles(ULP_CLOSE_ENERGIZE_US * ULP_RISCV_CYCLES_PER_US);

    // Release the magnet briefly and repeat the plate measurement before the
    // armature can move away. Only a successful second measurement permits
    // permanent holding current.
    gate_low();
    fsm_state = LOCK_FSM_CLOSE_VERIFY;
    close_verify_mode = 1;
    const uint32_t verified_state = measure_plate();
    close_verify_mode = 0;

    close_verify_state = verified_state;
    close_verify_feature = normalized_feature;
    close_verify_delta_raw = delta_raw;
    if (fault_flags != 0) {
        gate_low();
        return;
    }
    const uint32_t verify_feature = calibrated_verify_feature(close_verify_delta_raw);
    last_state = PLATE_UNKNOWN;
    uint32_t verify_state = verify_feature == 0
        ? PLATE_UNKNOWN : classify_feature(verify_feature);
    // GAP_1 may start the pull-in, but it can never directly authorize
    // permanent holding. A later series of cold CONTACT samples must start a
    // new attempt; its hot result is then checked on the calibrated scale.
    const bool verified_contact = detected_state == PLATE_CONTACT &&
        verify_state == PLATE_CONTACT;
    // Do not publish CONTACT when the stricter final check rejected it; this
    // also guarantees a one-sheet rejection beep instead of silent success.
    if (!verified_contact && verify_state == PLATE_CONTACT) {
        verify_state = PLATE_GAP_1;
    }
    close_verify_feature = verify_feature;
    close_verify_state = verify_state;
    if (verified_contact) {
        close_reject_latched = 0;
        gate_high();
        fsm_state = LOCK_FSM_LOCKED;
        lock_state = LOCK_STATE_LOCKED;
        raise_event(ULP_EVENT_LOCKED);
    } else {
        close_reject_counter++;
        close_reject_latched = 1;
        gate_low();
        fsm_state = LOCK_FSM_UNLOCKED_SCAN;
        lock_state = LOCK_STATE_UNLOCKED;
        raise_event(ULP_EVENT_CLOSE_REJECTED);
    }
}

static void open_from_locked(bool scan_after_open)
{
    fsm_state = LOCK_FSM_OPEN_DISCHARGE;
    gate_low();
    ulp_riscv_delay_cycles(ULP_OFF_GUARD_US * ULP_RISCV_CYCLES_PER_US);
    fsm_state = LOCK_FSM_OPEN_VERIFY_CURRENT_OFF;
    const int32_t residual = adc_average((int)active_sense_adc_channel, ULP_BASELINE_SAMPLES);
    coil_current_raw = residual < 0 ? 0 : (uint32_t)residual;
    if (residual < 0 || (uint32_t)residual > ULP_COIL_OFF_MAX_RAW) {
        enter_fault(ULP_FAULT_COIL_SHORT, ULP_EVENT_OPEN_FAILED);
        return;
    }
    lock_state = LOCK_STATE_UNLOCKED;
    fsm_state = LOCK_FSM_UNLOCKED_SCAN;
    raise_event(ULP_EVENT_UNLOCKED);
    if (scan_after_open) (void)measure_plate();
}

static int run_iteration(void)
{
    pulse_actual_us = 0;
    setup_gate();
    if (calibration_mode) {
        gate_low();
        lock_state = LOCK_STATE_UNLOCKED;
        if (calibration_verify_request) {
            calibration_verify_request = 0;
            fsm_state = LOCK_FSM_CLOSE_ENERGIZE;
            gate_high();
            ulp_riscv_delay_cycles(ULP_CLOSE_ENERGIZE_US * ULP_RISCV_CYCLES_PER_US);
            gate_low();
            fsm_state = LOCK_FSM_CLOSE_VERIFY;
            close_verify_mode = 1;
            (void)measure_plate();
            close_verify_mode = 0;
            calibration_verify_raw = delta_raw;
            calibration_verify_counter++;
            gate_low();
            lock_state = LOCK_STATE_UNLOCKED;
            return 0;
        }
        (void)measure_plate();
        return 0;
    }
    const uint32_t requested_command = read_command();

    if (requested_command == ULP_CMD_GATE_TEST && gate_test_ticks > 0) {
        gate_high();
        lock_state = LOCK_STATE_UNLOCKED;
        fsm_state = LOCK_FSM_GATE_TEST;
        plate_state = PLATE_UNKNOWN;
        normalized_feature = 0;
        pulse_actual_us = 0;
        ulp_riscv_delay_cycles(ULP_ADC_SETTLE_US * ULP_RISCV_CYCLES_PER_US);
        const int32_t first_sample = adc_read_channel((int)active_sense_adc_channel);
        int32_t sum = 0;
        bool burst_valid = first_sample >= 0;
        for (uint32_t i = 0; i < 8; i++) {
            ulp_riscv_delay_cycles(ULP_ADC_SETTLE_US * ULP_RISCV_CYCLES_PER_US);
            const int32_t sample = adc_read_channel((int)active_sense_adc_channel);
            if (sample < 0) {
                burst_valid = false;
                break;
            }
            sum += sample;
        }
        baseline_raw = first_sample;
        sense_raw = burst_valid ? sum / 8 : -1;
        delta_raw = burst_valid ? sense_raw - first_sample : -1;
        measurement_counter++;
        gate_test_ticks--;
        if (gate_test_ticks == 0) {
            gate_low();
            active_command = ULP_CMD_HOLD_OPEN;
            lock_state = LOCK_STATE_UNLOCKED;
            fsm_state = LOCK_FSM_OPEN_HOLD;
        }
        return 0;
    }

    if (requested_command == ULP_CMD_LOCK) {
        if (lock_state == LOCK_STATE_FAULT) {
            gate_low();
            fsm_state = LOCK_FSM_FAULT;
            const uint32_t retryable = ULP_FAULT_ADC_INVALID |
                ULP_FAULT_SENSE_INVALID | ULP_FAULT_COIL_OPEN;
            if (!fault_flags || (fault_flags & ~retryable) ||
                fault_retry_count >= ULP_FAULT_RETRY_LIMIT) return 0;
            /* Waiting is across wakeups: no busy loop, no repeated fault event.
             * The next attempt still performs the full contact verification. */
            fault_retry_wait_us += ULP_MEASURE_PERIOD_US;
            if (fault_retry_wait_us < ULP_FAULT_RETRY_DELAY_US) return 0;
            fault_retry_wait_us = 0;
            fault_retry_count++;
            fault_flags = 0;
            lock_state = LOCK_STATE_UNLOCKED;
            fsm_state = LOCK_FSM_UNLOCKED_SCAN;
            raise_event(ULP_EVENT_FAULT_RETRY);
        }
        if (lock_state != LOCK_STATE_LOCKED) {
            gate_low();
            fsm_state = LOCK_FSM_CLOSE_WAIT_PLATE;
            const uint32_t state = measure_plate();
            if (close_reject_latched) {
                // Wait indefinitely at any measured gap. As soon as the
                // calibrated classifier reports CONTACT, repeat the complete
                // close verification without requiring the door to open.
                if (state == PLATE_CONTACT) {
                    close_reject_latched = 0;
                    close_after_plate(state);
                }
                return 0;
            }
            if (state == PLATE_GAP_1) {
                close_after_plate(state);
            } else if (state == PLATE_CONTACT) {
                close_after_plate(state);
            }
        } else {
            fsm_state = LOCK_FSM_LOCKED;
            measure_battery_locked();
        }
        return 0;
    }

    if (requested_command == ULP_CMD_HOLD_OPEN) {
        if (lock_state == LOCK_STATE_LOCKED || fsm_state == LOCK_FSM_LOCKED) {
            open_from_locked(false);
        }
        if (lock_state != LOCK_STATE_FAULT) {
            gate_low();
            lock_state = LOCK_STATE_UNLOCKED;
            fsm_state = LOCK_FSM_OPEN_HOLD;
            plate_state = PLATE_UNKNOWN;
            normalized_feature = 0;
            pulse_actual_us = 0;
        }
        return 0;
    }

    if (requested_command != ULP_CMD_UNLOCK) {
        enter_fault(ULP_FAULT_STATE_INVALID, ULP_EVENT_OPEN_FAILED);
        return 0;
    }

    if (lock_state == LOCK_STATE_LOCKED || fsm_state == LOCK_FSM_LOCKED) {
        open_from_locked(true);
    } else if (lock_state != LOCK_STATE_FAULT) {
        gate_low();
        (void)measure_plate();
    }
    return 0;
}

/* One complete publication per wakeup, including fault/close verification.
 * Readers never observe LOCKED left over from a partially executed transition. */
int main(void)
{
    sequence++;
    __sync_synchronize();
    const uint32_t requested_sense_channel = sense_adc_channel;
    if (sense_adc_channel_valid(requested_sense_channel)) {
        if (active_sense_adc_channel != requested_sense_channel) {
            active_sense_adc_channel = requested_sense_channel;
            last_state = PLATE_UNKNOWN;
        }
    }
    int result = run_iteration();
    __sync_synchronize();
    sequence++;
    return result;
}
