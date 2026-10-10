"""Проверяет настоящий app_config_init в RISC-V/Unicorn с моделью NVS."""
import argparse, importlib.util, io, re, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / '.device-agent/test-deps'))
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_RISCV, UC_MODE_RISCV32
from unicorn.riscv_const import UC_RISCV_REG_SP, UC_RISCV_REG_GP, UC_RISCV_REG_RA, UC_RISCV_REG_A0, UC_RISCV_REG_PC

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('extract', ROOT / 'tools/test-ulp-regressions.py')
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    source = (ROOT / 'main/app_config.c').read_text(encoding='utf-8')
    functions = '\n'.join(helper.function(source, name) for name in ('set_defaults', 'values_valid', 'departure_pulse_valid', 'app_config_init'))
    work = ROOT / '.device-agent/app-config-default-tests'
    work.mkdir(parents=True, exist_ok=True)
    (work / 'esp_err.h').write_text('typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_NO_MEM 3\n')
    (work / 'app_build_number.h').write_text('#define APP_BUILD_NUMBER_STRING "test"\n')
    code = r'''
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "app_config.h"
#define NVS_NS "blelock"
#define NVS_READWRITE 1
#define ESP_ERR_NVS_NOT_FOUND 1
#define ESP_LOGW(...) ((void)0)
typedef int nvs_handle_t;
typedef void *SemaphoreHandle_t;
static app_config_t s_cfg;
static SemaphoreHandle_t s_config_mutex;
static int scenario, writes, commits, closes;
static bool namespace_exists, key_exists;
static uint8_t stored;
static SemaphoreHandle_t xSemaphoreCreateMutex(void){return (void*)1;}
static int nvs_open(const char *name,int mode,nvs_handle_t *h){
    if(scenario==7)return 9;
    if(strcmp(name,NVS_NS)||mode!=NVS_READWRITE)return 10;
    namespace_exists=true;*h=1;return 0;
}
static int nvs_get_i8(int h,const char *key,int8_t *value){return ESP_ERR_NVS_NOT_FOUND;}
static int nvs_get_u32(int h,const char *key,uint32_t *value){
    if(!strcmp(key,"departure_ms")){*value=20;return ESP_OK;}
    return ESP_ERR_NVS_NOT_FOUND;
}
static int nvs_set_u32(int h,const char *key,uint32_t value){return 12;}
static int nvs_get_u8(int h,const char *key,uint8_t *value){
    if(!strcmp(key,"serial_log"))return ESP_ERR_NVS_NOT_FOUND;
    if(strcmp(key,"departure_beep"))return 11;
    if(scenario==4)return 6;
    if(!key_exists)return ESP_ERR_NVS_NOT_FOUND;
    *value=stored;return 0;
}
static int nvs_set_u8(int h,const char *key,uint8_t value){
    if(strcmp(key,"departure_beep")||value!=1)return 12;
    writes++;if(scenario==5)return 7;
    stored=value;key_exists=true;return 0;
}
static int nvs_commit(int h){commits++;return scenario==6?8:0;}
static void nvs_close(int h){closes++;}
static int nvs_erase_key(int h,const char *key){return ESP_ERR_NVS_NOT_FOUND;}
static void remote_log_set_serial_enabled(bool enabled){}
FUNCTIONS
#define CHECK(x) do{if(!(x))return __LINE__;}while(0)
int run_tests(int test){
    scenario=test;namespace_exists=test!=0;key_exists=test==2||test==3;
    stored=test==2?0:1;
    int result=app_config_init();
    if(test>=4&&test<=7){
        CHECK(result==test+2);CHECK(closes==(test==7?0:1));
        if(test==4||test==7)CHECK(writes==0&&commits==0);
        if(test==5)CHECK(writes==1&&commits==0);
        if(test==6)CHECK(writes==1&&commits==1);
        return 0;
    }
    CHECK(result==0&&namespace_exists&&key_exists);
    CHECK(s_cfg.departure_buzzer_enabled==(test!=2));
    if(test==2||test==3)CHECK(writes==0&&commits==0);
    else CHECK(writes==1&&commits==1&&stored==1);
    if(test==8){CHECK(app_config_init()==0);CHECK(writes==1&&commits==1);}
    return 0;
}
'''.replace('FUNCTIONS', functions)
    (work / 'test.c').write_text(code, encoding='utf-8')
    output = work / 'test.elf'
    subprocess.run([args.cc, '-march=rv32imc_zicsr_zifencei', '-mabi=ilp32', '-O1', '-nostartfiles',
                    '-I', str(work), '-I', str(ROOT / 'main'), str(work / 'test.c'),
                    '-Wl,-Ttext=0x10000,-e,run_tests', '-lc', '-lgcc', '-lnosys', '-o', str(output)], check=True)
    elf = ELFFile(io.BytesIO(output.read_bytes()))
    symbols = elf.get_section_by_name('.symtab')
    addr = lambda name: symbols.get_symbol_by_name(name)[0]['st_value']
    segments = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
    base = min(s['p_vaddr'] for s in segments) & ~4095
    end = (max(s['p_vaddr'] + s['p_memsz'] for s in segments) + 4095) & ~4095
    for test in range(9):
        cpu = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        cpu.mem_map(base, end - base)
        cpu.mem_map(0x800000, 0x20000)
        cpu.mem_map(0x900000, 0x1000)
        for segment in segments: cpu.mem_write(segment['p_vaddr'], segment.data())
        cpu.reg_write(UC_RISCV_REG_SP, 0x81fff0)
        cpu.reg_write(UC_RISCV_REG_GP, addr('__global_pointer$'))
        cpu.reg_write(UC_RISCV_REG_RA, 0x900000)
        cpu.reg_write(UC_RISCV_REG_A0, test)
        cpu.emu_start(addr('run_tests'), 0x900000, timeout=10_000_000, count=10_000_000)
        assert cpu.reg_read(UC_RISCV_REG_PC) == 0x900000, 'Timeout'
        result = cpu.reg_read(UC_RISCV_REG_A0)
        assert result == 0, f'Scenario {test}: C assertion line {result}'
        print(f'PASS: app_config_init/NVS scenario {test}')

if __name__ == '__main__':
    main()
