"""Проверки production lock_task: приоритет, FAR, 20 секунд, ошибки и обычный AUTO.
Исполняется настоящий C-код в RISC-V/Unicorn; периферия и RTOS заменены стендом.
"""
import argparse, importlib.util, io, re, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/".device-agent/test-deps"))
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_RISCV,UC_MODE_RISCV32
from unicorn.riscv_const import *
def main():
    a=argparse.ArgumentParser();a.add_argument("--cc",required=True);args=a.parse_args()
    spec=importlib.util.spec_from_file_location("extract",ROOT/"tools/test-ulp-regressions.py")
    mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
    source=(ROOT/"main/lock_control.c").read_text(encoding="utf-8")
    types=source[source.index("typedef enum {"):source.index('static char s_source')]
    functions="\n".join(mod.function(source,n) for n in ("lock_control_internet_active","lock_task","lock_request_cloud"))
    work=ROOT/".device-agent/internet-regressions";work.mkdir(parents=True,exist_ok=True)
    (work/"experiment_config.h").write_text("#define ULP_EXPERIMENT_PERIOD_US 100000\n#define ULP_EXPERIMENT_OFF_GUARD_US 10000\n#define ULP_EXPERIMENT_PULSE_US 2000\n")
    code=r"""
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include <setjmp.h>
#include "shared_state.h"
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_TIMEOUT 2
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define TEST_OPEN_DURATION_MS 10000LL
static bool mock_compare_exchange(atomic_bool *p,bool *expected,bool desired){bool old=atomic_load(p);if(old!=*expected){*expected=old;return false;}atomic_store(p,desired);return true;}
#undef atomic_compare_exchange_strong
#define atomic_compare_exchange_strong(p,e,v) mock_compare_exchange(p,e,v)
typedef int esp_err_t;
typedef struct { int timer_a_ms; } app_config_t;
typedef struct { bool valid,hold_open; lock_state_t state; uint32_t fault_flags; } lock_feedback_t;
typedef struct { bool calibration_valid; unsigned calibration_mode,fault_flags; plate_state_t plate_state; } ulp_diagnostics_t;
TYPES
static bool s_hold_open=true,s_mode_persisted=true;
static atomic_bool s_internet,s_internet_pending;
static uint8_t s_internet_phase;
static int64_t s_internet_started_ms,s_internet_opened_ms,s_internet_started_us;
static uint64_t s_cloud_id;
static char s_source[12],s_actor[18];
static int s_events=1;
static int front_calls,front_failures,evicted;
static int xQueueSendToFront(int q,const lock_event_item_t *item,int wait){front_calls++;return front_calls<=front_failures?0:1;}
static int test_case,steps,open_calls,close_calls,hold_calls,held_saves,early_close,ignored_hold,open_delay_ms;
static bool requested;
static int64_t clock_ms;
static jmp_buf stop;
static bool lock_control_requested_open(void){return requested;}
static esp_err_t save_hold_open(bool held){held_saves++;return test_case==5 ? 1 : ESP_OK;}
static int64_t esp_timer_get_time(void){return clock_ms*1000;}
static void app_config_get(app_config_t *cfg){cfg->timer_a_ms=500;}
static void event_log_add(const char *s,...){}
static esp_err_t apply_command(ulp_command_t cmd){
    if(cmd==ULP_CMD_HOLD_OPEN)hold_calls++;
    if(cmd==ULP_CMD_OPEN)open_calls++;
    if(cmd==ULP_CMD_CLOSE){close_calls++;if(clock_ms<20000+open_delay_ms && test_case!=1&&test_case!=6&&test_case!=7&&test_case!=8)early_close++;}
    requested=cmd!=ULP_CMD_CLOSE;return 0;
}
static esp_err_t apply_state(bool open){return apply_command(open?ULP_CMD_OPEN:ULP_CMD_CLOSE);}
static bool ulp_controller_read_diagnostics(ulp_diagnostics_t *d){
    *d=(ulp_diagnostics_t){.calibration_valid=test_case!=4,.fault_flags=test_case==3?1:0,
        .plate_state=(test_case==1&&clock_ms>=2100)?PLATE_FAR:PLATE_CONTACT};return true;
}
static void lock_control_get_feedback(lock_feedback_t*f){
    f->valid=true;f->fault_flags=(test_case==6&&clock_ms>=2100)?1:0;f->hold_open=s_hold_open;
    f->state=requested ? (clock_ms>=open_delay_ms?LOCK_STATE_UNLOCKED:LOCK_STATE_LOCKED) : LOCK_STATE_LOCKED;
}
static void refresh_feedback(int64_t t){}
static void xQueueReset(int q){}
static int xQueueReceive(int q,lock_event_item_t *item,int wait){
    if(test_case==9||test_case==10){evicted++;return 1;}
    if(steps++>=300)longjmp(stop,1);
    clock_ms=(steps-1)*100;
    *item=(lock_event_item_t){0};
    if(steps==1){
        if(test_case==7){item->kind=LOCK_EVENT_OPEN;s_hold_open=false;}
        else{item->kind=LOCK_EVENT_INTERNET_OPEN;item->cloud_id=11;if(test_case==12)item->deadline_us=-1;}
        return 1;
    }
    if(test_case==2&&(steps==8||steps==15)){item->kind=LOCK_EVENT_SET_HOLD_OPEN;ignored_hold++;return 1;}
    if(test_case==2&&steps==20){item->kind=LOCK_EVENT_OPEN;return 1;}
    return 0;
}
FUNCTIONS
#define CHECK(x) do{if(!(x))return __LINE__;}while(0)
int run_tests(int scenario){
    test_case=scenario;steps=0;requested=false;s_hold_open=scenario!=7;s_internet_phase=0;
    open_delay_ms=(scenario==8?5000:0);
    if(scenario==9||scenario==10||scenario==11){
        front_failures=scenario==9?1:2;
        if(scenario==11)atomic_store(&s_internet,true);
        int rc=lock_request_cloud("internet_open","04c8e193a7065",11,10000000);
        if(scenario==9){
            CHECK(rc==ESP_OK&&front_calls==2&&evicted==1&&lock_control_internet_active());
            CHECK(lock_request_cloud("internet_open","04c8e193a7065",12,10000000)==ESP_ERR_INVALID_STATE);
        }else if(scenario==10)CHECK(rc==ESP_ERR_TIMEOUT&&!lock_control_internet_active()&&front_calls==2);
        else CHECK(rc==ESP_ERR_INVALID_STATE&&front_calls==0);
        return 0;
    }
    if(scenario==12)atomic_store(&s_internet_pending,true);
    if(setjmp(stop)==0)lock_task(NULL);
    if(scenario==0||scenario==2||scenario==8){
        CHECK(open_calls==1&&close_calls==1&&early_close==0&&hold_calls==0&&s_internet_phase==4&&!s_hold_open);
        if(scenario==2)CHECK(ignored_hold==2);
    }else if(scenario==1){CHECK(open_calls==1&&close_calls==1&&s_internet_phase==4&&clock_ms>=2100);}
    else if(scenario==3||scenario==4){CHECK(open_calls==0&&s_internet_phase==5);}
    else if(scenario==5){CHECK(open_calls==0&&s_hold_open&&s_internet_phase==5);}
    else if(scenario==6){CHECK(open_calls==1&&close_calls==1&&s_internet_phase==5);}
    else if(scenario==7){CHECK(open_calls==1&&close_calls==1&&s_internet_phase==0);}
    else if(scenario==12)CHECK(open_calls==0&&!lock_control_internet_active()&&s_internet_phase==0);
    return 0;
}
""".replace("TYPES",types).replace("FUNCTIONS",functions)
    (work/"test.c").write_text(code,encoding="utf-8")
    elf_path=work/"test.elf"
    subprocess.run([args.cc,"-march=rv32imc_zicsr_zifencei","-mabi=ilp32","-O1","-nostartfiles",
        "-I",str(work),"-I",str(ROOT/"main/ulp"),str(work/"test.c"),"-Wl,-Ttext=0x10000,-e,run_tests",
        "-lc","-lgcc","-lnosys","-o",str(elf_path)],check=True)
    elf=ELFFile(io.BytesIO(elf_path.read_bytes()));syms=elf.get_section_by_name(".symtab")
    addr=lambda n:syms.get_symbol_by_name(n)[0]["st_value"]
    segments=[s for s in elf.iter_segments() if s["p_type"]=="PT_LOAD"]
    base=min(s["p_vaddr"] for s in segments)&~4095
    end=(max(s["p_vaddr"]+s["p_memsz"] for s in segments)+4095)&~4095
    for test in range(13):
        uc=Uc(UC_ARCH_RISCV,UC_MODE_RISCV32);uc.mem_map(base,end-base)
        uc.mem_map(0x800000,0x20000);uc.mem_map(0x900000,0x1000)
        for seg in segments:uc.mem_write(seg["p_vaddr"],seg.data())
        uc.reg_write(UC_RISCV_REG_SP,0x81fff0);uc.reg_write(UC_RISCV_REG_GP,addr("__global_pointer$"))
        uc.reg_write(UC_RISCV_REG_RA,0x900000);uc.reg_write(UC_RISCV_REG_A0,test)
        uc.emu_start(addr("run_tests"),0x900000,timeout=10_000_000,count=10_000_000)
        assert uc.reg_read(UC_RISCV_REG_PC)==0x900000,"Timeout"
        result=uc.reg_read(UC_RISCV_REG_A0)
        assert result==0,f"Scenario {test}: assertion C line {result}"
        print(f"PASS: production internet lock scenario {test}")
if __name__=="__main__":main()
