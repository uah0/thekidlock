"""Проверка реального C event_log/remote_log в RISC-V (без ESP32/удаления данных)."""
import argparse
import io
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / '.device-agent/test-deps'))
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_RISCV, UC_MODE_RISCV32
from unicorn.riscv_const import UC_RISCV_REG_SP, UC_RISCV_REG_RA, UC_RISCV_REG_GP, UC_RISCV_REG_PC, UC_RISCV_REG_A0

parser = argparse.ArgumentParser()
parser.add_argument('--cc', required=True)
args = parser.parse_args()
work = ROOT / '.device-agent/log-boundaries'
(work / 'freertos').mkdir(parents=True, exist_ok=True)
stubs = {
    'freertos/FreeRTOS.h': '''#pragma once
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define portMAX_DELAY 0
''',
    'freertos/semphr.h': '''#pragma once
typedef void *SemaphoreHandle_t;
#define xSemaphoreCreateMutex() ((void*)1)
#define xSemaphoreTake(a,b) ((void)(a))
#define xSemaphoreGive(a) ((void)(a))
''',
    'esp_log.h': '''#pragma once
#include <stdarg.h>
typedef int (*vprintf_like_t)(const char *, va_list);
#define ESP_LOGI(...) ((void)0)
static inline vprintf_like_t esp_log_set_vprintf(vprintf_like_t f) { return f; }
''',
    'esp_timer.h': '#include <stdint.h>\nstatic inline int64_t esp_timer_get_time(void) { return 0; }\n',
}
for name, contents in stubs.items():
    (work / name).write_text(contents)
code = r'''
#include "EVENT_SOURCE"
#include "REMOTE_SOURCE"
#define CHECK(x) do { if (!(x)) return __LINE__; } while(0)
char output[528];
int run_tests(int capacity) {
    char cut[] = {'A', (char)0xd0, (char)0x91, (char)0xd0, 0};
    trim_utf8(cut);
    CHECK(strlen(cut) == 3 && cut[2] == (char)0x91);
    char complete[] = {(char)0xf0,(char)0x9f,(char)0x94,(char)0x92,0};
    trim_utf8(complete); CHECK(strlen(complete) == 4);
    s_count=LOG_ENTRIES; s_head=0;
    for (int i=0;i<LOG_ENTRIES;i++) {
        strcpy(s_entries[i], "quote\" slash\\ ");
        int pos=strlen(s_entries[i]);
        for (int j=0;j<30;j++) { s_entries[i][pos++]=(char)0xd0; s_entries[i][pos++]=(char)0x91; }
        s_entries[i][pos]=0;
    }
    memset(output,0xa5,sizeof(output));
    size_t n=event_log_to_json(output+8,capacity);
    CHECK(n < (size_t)capacity && output[8+n]==0);
    for(int i=0;i<8;i++) CHECK((unsigned char)output[i]==0xa5);
    for(unsigned i=8+capacity;i<sizeof(output);i++) CHECK((unsigned char)output[i]==0xa5);
    char block[512], result[4096];
    for(int i=0;i<512;i++) block[i]=(char)i;
    for(int i=0;i<100;i++) append_bytes(block,sizeof(block));
    uint64_t next,oldest; bool dropped;
    n=remote_log_read(0,result,sizeof(result),&next,&oldest,&dropped);
    CHECK(n==1024 && dropped && oldest==51200-32768 && next==oldest+n);
    for(unsigned i=0;i<n;i++) CHECK(result[i]==(char)(oldest+i));
    n=remote_log_read(next,result,17,&next,&oldest,&dropped);
    CHECK(n==17 && !dropped);
    n=remote_log_read(51200,result,sizeof(result),&next,&oldest,&dropped);
    CHECK(n==0 && !dropped && next==51200);
    n=remote_log_read(999999,result,sizeof(result),&next,&oldest,&dropped);
    CHECK(n==1024 && dropped);
    return 0;
}
'''.replace('EVENT_SOURCE', (ROOT / 'main/event_log.c').as_posix()).replace('REMOTE_SOURCE', (ROOT / 'main/remote_log.c').as_posix())
(work / 'test.c').write_text(code)
elf_path = work / 'test.elf'
subprocess.run([args.cc, '-march=rv32im', '-mabi=ilp32', '-O1', '-ffreestanding', '-nostdlib',
                '-fno-builtin', '-ffunction-sections', '-fdata-sections', '-I', str(work),
                str(work / 'test.c'), '-Wl,-Ttext=0x10000,-e,run_tests,--gc-sections',
                '-lc', '-lgcc', '-o', str(elf_path)], check=True)
elf = ELFFile(io.BytesIO(elf_path.read_bytes()))
symbols = elf.get_section_by_name('.symtab')
def address(name):
    return symbols.get_symbol_by_name(name)[0]['st_value']
segments = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
base = min(s['p_vaddr'] for s in segments) & ~4095
end = (max(s['p_vaddr'] + s['p_memsz'] for s in segments) + 4095) & ~4095
for capacity in (3,4,5,8,16,64,76,95,96,97,128,191,192,256,511):
    uc = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
    uc.mem_map(base, end-base)
    uc.mem_map(0x800000, 0x20000)
    uc.mem_map(0x900000, 0x1000)
    for segment in segments:
        uc.mem_write(segment['p_vaddr'], segment.data())
    uc.reg_write(UC_RISCV_REG_SP, 0x81fff0)
    uc.reg_write(UC_RISCV_REG_GP, address('__global_pointer$'))
    uc.reg_write(UC_RISCV_REG_RA, 0x900000)
    uc.reg_write(UC_RISCV_REG_A0, capacity)
    uc.emu_start(address('run_tests'), 0x900000, timeout=10_000_000, count=10_000_000)
    assert uc.reg_read(UC_RISCV_REG_PC) == 0x900000, 'timeout'
    assert uc.reg_read(UC_RISCV_REG_A0) == 0, f'C assertion line {uc.reg_read(UC_RISCV_REG_A0)}'
    payload = bytes(uc.mem_read(address('output')+8, capacity)).split(b'\0',1)[0]
    json.loads(payload.decode('utf-8'))
print('PASS: 15 buffer boundaries, UTF-8/JSON, ring wrap/loss/cursors/read limit')
