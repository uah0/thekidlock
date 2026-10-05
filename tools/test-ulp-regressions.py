"""Исполнение реального ULP C-кода в RISC-V эмуляторе, ADC/GPIO заменены стендом.
Зависимости: unicorn, pyelftools (можно установить в .device-agent/test-deps).
Запуск: python tools/test-ulp-regressions.py --cc PATH_TO_riscv32-esp-elf-gcc
Это проверка логики, не электрических параметров/таймингов оборудования.
"""
import argparse
import io
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / '.device-agent/test-deps'))
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_RISCV, UC_MODE_RISCV32
from unicorn.riscv_const import UC_RISCV_REG_SP, UC_RISCV_REG_RA, UC_RISCV_REG_A0, UC_RISCV_REG_GP, UC_RISCV_REG_PC


def function(source, name):
    """Extract the real production function, not a reimplementation of its policy."""
    import re
    match = re.search(r'^(?:static )?[\w *]+\b' + name + r'\([^;]*?\)\s*\{', source, re.M)
    if not match:
        raise ValueError(name)
    start = source.index('{', match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', required=True)
    args = parser.parse_args()
    work = ROOT / '.device-agent/ulp-regressions'
    (work / 'hal').mkdir(parents=True, exist_ok=True)
    stubs = {
        'ulp_riscv.h': '#include <stdbool.h>\n',
        'ulp_riscv_utils.h': '#define ULP_RISCV_CYCLES_PER_US 17.5\n#define ulp_riscv_delay_cycles(x) ((void)(x))\n',
        'ulp_riscv_gpio.h': '''#define GPIO_NUM_9 9
#define RTCIO_MODE_OUTPUT 1
#define ulp_riscv_gpio_init(x) ((void)(x))
#define ulp_riscv_gpio_output_enable(x) ((void)(x))
#define ulp_riscv_gpio_set_output_mode(x,y) ((void)0)
#define ulp_riscv_gpio_output_level(x,y) ((void)0)
''',
        'ulp_riscv_adc_ulp_core.h': 'int ulp_riscv_adc_read_channel(int unit, int channel);\n',
        'hal/adc_ll.h': '''#define ADC_UNIT_1 0
#define ADC_ATTEN_DB_12 3
#define adc_oneshot_ll_set_atten(a,b,c) ((void)0)
''',
    }
    for name, data in stubs.items():
        (work / name).write_text(data)
    controller = (ROOT / 'main/ulp_controller.c').read_text(encoding='utf-8')
    calibration = '\n'.join(function(controller, name) for name in (
        'calibration_end', 'calibration_expire', 'calibration_timeout',
        'ulp_controller_calibration_start', 'ulp_controller_calibration_cancel',
        'ulp_controller_calibration_verify', 'ulp_controller_calibration_save'))
    import re
    feedback_header = (ROOT / 'main/lock_control.h').read_text(encoding='utf-8')
    feedback = re.search(r'typedef struct \{.*?\} lock_feedback_t;', feedback_header, re.S)[0]
    feedback += '\n' + function((ROOT / 'main/lock_control.c').read_text(encoding='utf-8'), 'lock_control_feedback_name')
    code = r'''
#include <stdint.h>
#include <stdbool.h>
#define main ulp_step
#include "ULP_SOURCE"
#undef main
static int injected_adc_failure, injected_bad_contact;
int ulp_riscv_adc_read_channel(int unit, int channel)
{
    (void)unit;
    if (injected_adc_failure) return -1;
    if (channel == ADC_VBAT_CHANNEL) return 700;
    if (!gate_level) return 0;
    return close_verify_mode ? (injected_bad_contact ? 700 : 400) : 200;
}

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define portMAX_DELAY 0xffffffffu
#define pdTRUE 1
static bool s_initialized = true;
static int s_calibration_mutex = 1;
static int64_t s_calibration_deadline_us, mock_time;
static uint32_t s_calibration_timeout_count;
static int semaphore_available = 1;
#define xSemaphoreTake(a,b) (semaphore_available)
#define xSemaphoreGive(a) ((void)0)
#define event_log_add(...) ((void)0)
#define esp_timer_get_time() mock_time
#define ulp_calibration_mode calibration_mode
#define ulp_calibration_verify_request calibration_verify_request
#define ulp_close_reject_latched close_reject_latched
#define ulp_calibration_contact_raw calibration_contact_raw
#define ulp_calibration_paper1_raw calibration_paper1_raw
#define ulp_calibration_paper2_raw calibration_paper2_raw
#define ulp_calibration_open_raw calibration_open_raw
#define ulp_calibration_verify_contact_raw calibration_verify_contact_raw
#define ULP_CAL_NS "test"
#define NVS_READWRITE 1
typedef int nvs_handle_t;
static int nvs_writes;
static uint8_t s_sense_gpio = 7;
static int nvs_open(const char *name, int mode, nvs_handle_t *handle) { *handle=1; return 0; }
static int nvs_set_i32(nvs_handle_t h, const char *key, int32_t v) { nvs_writes++; return 0; }
static int nvs_set_u8(nvs_handle_t h, const char *key, uint8_t v) { nvs_writes++; return 0; }
static int nvs_commit(nvs_handle_t h) { return 0; }
static void nvs_close(nvs_handle_t h) {}
CALIBRATION_FUNCTIONS
FEEDBACK_FUNCTIONS
static bool text_equals(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int run_tests(int test)
{
    calibration_contact_raw = 200;
    calibration_paper1_raw = 340;
    calibration_paper2_raw = 480;
    calibration_open_raw = 1100;
    calibration_verify_contact_raw = 400;
    command = ULP_CMD_LOCK;
    command_sequence = 2;
    if (test == 0) { /* Healthy full close and acknowledgement. */
        ulp_step();
        CHECK(lock_state == LOCK_STATE_LOCKED && gate_level == 1);
        CHECK(command_ack_sequence == 2 && sequence == 2);
    } else if (test == 1) { /* One ADC fault recovers, never energizes during cooldown. */
        injected_adc_failure = 1;
        ulp_step();
        CHECK(lock_state == LOCK_STATE_FAULT && gate_level == 0);
        injected_adc_failure = 0;
        for (unsigned i = 0; i < ULP_FAULT_RETRY_DELAY_US / ULP_MEASURE_PERIOD_US - 1; ++i) {
            ulp_step();
            CHECK(lock_state == LOCK_STATE_FAULT && gate_level == 0);
        }
        ulp_step();
        CHECK(fault_retry_count == 1 && lock_state == LOCK_STATE_LOCKED && gate_level == 1);
    } else if (test == 2) { /* Persistent failure exhausts its bounded budget. */
        injected_adc_failure = 1;
        for (int i = 0; i < 700; ++i) ulp_step();
        CHECK(fault_retry_count == 3 && lock_state == LOCK_STATE_FAULT && gate_level == 0);
        uint32_t old_events = event_sequence;
        for (int i = 0; i < 700; ++i) ulp_step();
        CHECK(event_sequence == old_events && fault_retry_count == 3);
    } else if (test == 3) { /* A short is never retried automatically. */
        lock_state = LOCK_STATE_FAULT;
        fault_flags = ULP_FAULT_COIL_SHORT;
        for (int i = 0; i < 700; ++i) ulp_step();
        CHECK(fault_retry_count == 0 && lock_state == LOCK_STATE_FAULT && gate_level == 0);
    } else if (test == 4) { /* Retry cannot bypass the hot contact check. */
        injected_adc_failure = 1;
        ulp_step();
        injected_adc_failure = 0;
        injected_bad_contact = 1;
        for (unsigned i = 0; i < ULP_FAULT_RETRY_DELAY_US / ULP_MEASURE_PERIOD_US; ++i) ulp_step();
        CHECK(fault_retry_count == 1 && close_reject_latched == 1);
        CHECK(lock_state == LOCK_STATE_UNLOCKED && gate_level == 0);
    } else if (test == 5) { /* Odd (in-flight) command is never accepted. */
        command_sequence = 1;
        ulp_step();
        CHECK(command_ack_sequence == 0 && lock_state == LOCK_STATE_UNLOCKED && gate_level == 0);
    } else if (test == 6) { /* Explicit UNLOCK cancels fault/retry and does not close. */
        injected_adc_failure = 1;
        ulp_step();
        injected_adc_failure = 0;
        command = ULP_CMD_UNLOCK;
        command_sequence = 4;
        for (int i = 0; i < 700; ++i) ulp_step();
        CHECK(command_ack_sequence == 4 && fault_retry_count == 0);
        CHECK(lock_state == LOCK_STATE_UNLOCKED && gate_level == 0);
    } else if (test == 7) { /* Calibration expires without a browser request. */
        CHECK(ulp_controller_calibration_start() == ESP_OK);
        CHECK(calibration_mode == 1);
        int64_t deadline = s_calibration_deadline_us;
        mock_time = 60000000;
        CHECK(ulp_controller_calibration_start() == ESP_ERR_INVALID_STATE);
        CHECK(s_calibration_deadline_us == deadline);
        mock_time = ULP_CALIBRATION_TIMEOUT_US - 1;
        calibration_timeout(0);
        CHECK(calibration_mode == 1);
        mock_time++;
        calibration_timeout(0);
        CHECK(calibration_mode == 0 && s_calibration_timeout_count == 1);
        CHECK(ulp_controller_calibration_verify() == ESP_ERR_INVALID_STATE);
        ulp_step();
        CHECK(lock_state == LOCK_STATE_LOCKED); /* Latest command resumes. */
    } else if (test == 8) { /* An NVS holder does not block esp_timer; next tick expires. */
        CHECK(ulp_controller_calibration_start() == ESP_OK);
        mock_time = ULP_CALIBRATION_TIMEOUT_US;
        semaphore_available = 0;
        calibration_timeout(0);
        CHECK(calibration_mode == 1);
        semaphore_available = 1;
        calibration_timeout(0);
        CHECK(calibration_mode == 0);
    } else if (test == 9) { /* Calibration cancellation never closes a requested-open lock. */
        command = ULP_CMD_UNLOCK;
        CHECK(ulp_controller_calibration_start() == ESP_OK);
        ulp_step();
        ulp_controller_calibration_cancel();
        ulp_step();
        CHECK(calibration_mode == 0 && lock_state == LOCK_STATE_UNLOCKED && gate_level == 0);
    } else if (test == 10) {
        CHECK(ulp_controller_calibration_start() == ESP_OK);
        CHECK(ulp_controller_calibration_save(100,4096,300) == ESP_ERR_INVALID_ARG);
        CHECK(ulp_controller_calibration_save(100,1000,4096) == ESP_ERR_INVALID_ARG);
        CHECK(ulp_controller_calibration_save(100,100,300) == ESP_ERR_INVALID_ARG);
        CHECK(ulp_controller_calibration_save(-1,1000,300) == ESP_ERR_INVALID_ARG);
        CHECK(nvs_writes == 0 && calibration_mode == 1);
        CHECK(ulp_controller_calibration_save(100,4095,4095) == ESP_OK);
        CHECK(nvs_writes == 6 && calibration_mode == 0);
        CHECK(calibration_paper1_raw == 750 && calibration_paper2_raw == 1400);
    } else if (test == 11) { /* Missing NVS calibration never closes silently. */
        calibration_contact_raw = -1;
        calibration_paper1_raw = -1;
        calibration_paper2_raw = -1;
        calibration_open_raw = -1;
        calibration_verify_contact_raw = -1;
        for (int i = 0; i < 10; ++i) ulp_step();
        CHECK(lock_state == LOCK_STATE_UNLOCKED && gate_level == 0);
        CHECK(plate_state == PLATE_UNKNOWN && normalized_feature == 0);
        CHECK(close_attempt_counter == 0);
        lock_feedback_t feedback = {.state=LOCK_STATE_UNLOCKED, .valid=true, .calibration_required=true};
        CHECK(text_equals(lock_control_feedback_name(&feedback), "CALIBRATION_REQUIRED"));
        feedback.calibrating = true;
        CHECK(text_equals(lock_control_feedback_name(&feedback), "CALIBRATING"));
    } else if (test == 12) { /* Invalid hot reference cannot masquerade as calibrated. */
        calibration_verify_contact_raw = -1;
        ulp_step();
        CHECK(normalized_feature == 0 && close_attempt_counter == 0 && gate_level == 0);
        CHECK(!ulp_calibration_values_valid(100,200,300,5000,400));
        CHECK(!ulp_calibration_values_valid(100,100,300,1000,400));
        CHECK(ulp_calibration_values_valid(100,200,300,1000,400));
    } else return -1;
    CHECK(!(sequence & 1));
    return 0;
}
'''.replace('ULP_SOURCE', (ROOT / 'main/ulp/main.c').as_posix()).replace('CALIBRATION_FUNCTIONS', calibration).replace('FEEDBACK_FUNCTIONS', feedback)
    (work / 'test.c').write_text(code, encoding='utf-8')
    elf_path = work / 'test.elf'
    subprocess.run([args.cc, '-march=rv32im', '-mabi=ilp32', '-O1', '-ffreestanding',
                    '-nostdlib', '-fno-builtin', '-I', str(work), str(work / 'test.c'),
                    '-Wl,-Ttext=0x10000,-e,run_tests', '-lgcc', '-o', str(elf_path)], check=True)
    elf = ELFFile(io.BytesIO(elf_path.read_bytes()))
    entry = elf.get_section_by_name('.symtab').get_symbol_by_name('run_tests')[0]['st_value']
    for index in range(13):
        uc = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        segments = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
        base = min(s['p_vaddr'] for s in segments) & ~4095
        end = (max(s['p_vaddr'] + s['p_memsz'] for s in segments) + 4095) & ~4095
        uc.mem_map(base, end - base)
        uc.mem_map(0x800000, 0x20000)
        uc.mem_map(0x900000, 0x1000)
        for segment in elf.iter_segments():
            if segment['p_type'] == 'PT_LOAD':
                uc.mem_write(segment['p_vaddr'], segment.data())
        uc.reg_write(UC_RISCV_REG_SP, 0x81fff0)
        gp = elf.get_section_by_name('.symtab').get_symbol_by_name('__global_pointer$')
        if gp:
            uc.reg_write(UC_RISCV_REG_GP, gp[0]['st_value'])
        uc.reg_write(UC_RISCV_REG_RA, 0x900000)
        uc.reg_write(UC_RISCV_REG_A0, index)
        uc.emu_start(entry, 0x900000, timeout=10_000_000, count=10_000_000)
        result = uc.reg_read(UC_RISCV_REG_A0)
        if uc.reg_read(UC_RISCV_REG_PC) != 0x900000:
            raise AssertionError(f'test {index}: execution did not return')
        if result:
            raise AssertionError(f'test {index}: production-code assertion failed at C line {result}')
        print(f'PASS ULP/calibration scenario {index}')


if __name__ == '__main__':
    main()
