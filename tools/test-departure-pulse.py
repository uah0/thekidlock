"""Проверяет хранение длительности бипера в настоящих config-функциях и HTTP handler."""
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
    functions = '\n'.join(helper.function(source, name) for name in ('set_defaults', 'values_valid', 'departure_pulse_valid', 'app_config_init', 'app_config_set_departure_buzzer_pulse_ms', 'app_config_get'))
    web_source = (ROOT / 'main/web_ui.c').read_text(encoding='utf-8')
    functions += '\n' + '\n'.join(helper.function(web_source, name) for name in ('departure_pulse_from_request', 'departure_buzzer_pulse_post', 'departure_buzzer_preview_post'))
    main_source = (ROOT / 'main/main.c').read_text(encoding='utf-8')
    functions += '\n' + '\n'.join(helper.function(main_source, name) for name in ('departure_pulse_off', 'departure_buzzer_tone', 'app_buzzer_request_departure_preview', 'departure_preview_active'))
    work = ROOT / '.device-agent/departure-pulse-tests'
    work.mkdir(parents=True, exist_ok=True)
    (work / 'esp_err.h').write_text('typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_NO_MEM 3\n#define ESP_ERR_INVALID_ARG 4\n#define ESP_ERR_INVALID_STATE 5\n')
    (work / 'app_build_number.h').write_text('#define APP_BUILD_NUMBER_STRING "test"\n')
    code = r'''
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <reent.h>
struct _reent *__getreent(void){static struct _reent state=_REENT_INIT(state);return &state;}
#include "app_config.h"
#define NVS_NS "blelock"
#define NVS_READWRITE 1
#define ESP_ERR_NVS_NOT_FOUND 1
#define ESP_LOGW(...) ((void)0)
#define portMAX_DELAY 0
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
typedef int nvs_handle_t;
typedef void *SemaphoreHandle_t;
typedef struct {const char *duration;} httpd_req_t;
static app_config_t s_cfg;
static SemaphoreHandle_t s_config_mutex;
static int scenario,writes,commits,closes,locks,setting;
static bool exists,pending;
static uint32_t stored,staged;
static SemaphoreHandle_t xSemaphoreCreateMutex(void){return (void*)1;}
static void xSemaphoreTake(void *h,int timeout){locks++;}
static void xSemaphoreGive(void *h){locks--;}
static int nvs_open(const char *name,int mode,nvs_handle_t *h){
 if(setting&&scenario==8)return 9;*h=1;return ESP_OK;
}
static int nvs_get_i8(int h,const char *k,int8_t *v){return ESP_ERR_NVS_NOT_FOUND;}
static int nvs_get_u8(int h,const char *k,uint8_t *v){*v=0;return ESP_OK;}
static int nvs_get_u32(int h,const char *k,uint32_t *v){
 if(strcmp(k,"departure_ms"))return ESP_ERR_NVS_NOT_FOUND;
 if(scenario==3)return 6;
 if(!exists)return ESP_ERR_NVS_NOT_FOUND;*v=stored;return ESP_OK;
}
static int nvs_set_u8(int h,const char *k,uint8_t v){return 99;}
static int nvs_set_u32(int h,const char *k,uint32_t v){
 if(strcmp(k,"departure_ms"))return 99;writes++;
 if(scenario==4||(setting&&scenario==6))return 7;
 staged=v;pending=true;return ESP_OK;
}
static int nvs_commit(int h){
 commits++;if(scenario==5||(setting&&scenario==7))return 8;
 if(pending){stored=staged;exists=true;pending=false;}return ESP_OK;
}
static void nvs_close(int h){closes++;pending=false;}
static int nvs_erase_key(int h,const char *k){return ESP_ERR_NVS_NOT_FOUND;}
static void remote_log_set_serial_enabled(bool enabled){}
static int httpd_req_get_url_query_str(httpd_req_t *r,char *out,unsigned len){
 if(!r->duration)return 1;strncpy(out,r->duration,len-1);out[len-1]=0;return ESP_OK;
}
static int httpd_query_key_value(const char *q,const char *k,char *out,unsigned len){
 if(strlen(q)>=len)return 1;strcpy(out,q);return ESP_OK;
}
static int httpd_resp_send_err(httpd_req_t *r,int status,const char *msg){return status;}
static int send_json(httpd_req_t *r,const char *msg){return ESP_OK;}

#define APP_BUZZER_GPIO 8
#define APP_BUZZER_ACTIVE_LEVEL 0
#define ESP_LOGE(...) ((void)0)
#define pdMS_TO_TICKS(ms) ((ms)/10)
typedef uint32_t TickType_t;
typedef void *esp_timer_handle_t;
typedef struct {uint32_t command_overlap_counter;} ulp_diagnostics_t;
#define portENTER_CRITICAL(m) ((void)0)
#define portEXIT_CRITICAL(m) ((void)0)
static int s_departure_preview_mux;
static uint32_t s_departure_preview_pulse_ms;
static int64_t s_departure_preview_until_us,now_us;
static bool s_departure_preview_ready;
static int64_t esp_timer_get_time(void){return now_us;}
static int app_buzzer_request_departure_preview(uint32_t pulse_ms);
static uint32_t ticks,on_timeout,off_at,level,timer_active,timer_due,timer_error,overlap;
static int gpio_set_level(int pin,int value){level=value;return 0;}
static uint32_t xTaskGetTickCount(void){return ticks;}
static void departure_pulse_off(void *arg);
static int esp_timer_start_once(void *timer,uint64_t timeout){
 if(timer_error)return 1;on_timeout=timeout;timer_due=ticks*10000+timeout;timer_active=1;return 0;
}
static int esp_timer_stop(void *timer){timer_active=0;return 0;}
static void vTaskDelayUntil(TickType_t *wake,TickType_t slice){
 *wake+=slice;ticks=*wake;
 if(timer_active&&ticks*10000>=timer_due){off_at=timer_due;timer_active=0;departure_pulse_off(0);}
}
static void vTaskDelay(TickType_t delay){ticks+=delay;}
static bool ulp_controller_read_diagnostics(ulp_diagnostics_t *d){d->command_overlap_counter=overlap;return true;}
FUNCTIONS
#define CHECK(x) do{if(!(x))return __LINE__;}while(0)
int run_tests(int test){
 scenario=test;exists=test==1||test>=6;stored=test==1?60:20;
 if(test==2){exists=true;stored=161;}
 int result=app_config_init();
 if(test>=3&&test<=5){CHECK(result==test+3);CHECK(closes==1);return 0;}
 CHECK(result==0);CHECK(!s_cfg.departure_buzzer_enabled);CHECK(!s_cfg.serial_log_enabled);
 CHECK(s_cfg.departure_buzzer_pulse_ms==(test==1?60:20));
 if(test==0||test==2){CHECK(writes==1&&commits==1&&stored==20);CHECK(app_config_init()==0);CHECK(writes==1&&commits==1);}
 if(test>=6&&test<=8){
  setting=1;result=app_config_set_departure_buzzer_pulse_ms(100);
  CHECK(result==test+1);CHECK(s_cfg.departure_buzzer_pulse_ms==20&&stored==20);CHECK(locks==0);return 0;
 }
 if(test==9){
  CHECK(app_config_set_departure_buzzer_pulse_ms(100)==0);CHECK(stored==100&&s_cfg.departure_buzzer_pulse_ms==100);CHECK(writes==1&&commits==1);
  CHECK(app_config_init()==0);CHECK(s_cfg.departure_buzzer_pulse_ms==100);CHECK(writes==1&&commits==1);
  CHECK(app_config_set_departure_buzzer_pulse_ms(100)==0);CHECK(writes==1&&commits==1);
 }
 if(test==10){
  uint32_t invalid[]={0,1,4,161,UINT32_MAX};for(unsigned i=0;i<5;i++)CHECK(app_config_set_departure_buzzer_pulse_ms(invalid[i])==4);
  CHECK(writes==0&&commits==0&&locks==0);
 }
 if(test==11){CHECK(app_config_set_departure_buzzer_pulse_ms(5)==0);CHECK(app_config_set_departure_buzzer_pulse_ms(160)==0);CHECK(stored==160&&s_cfg.departure_buzzer_pulse_ms==160);}
 if(test==12){
  const char *bad[]={NULL,"","-10","+20"," 20","20x","4","161","200","42949672960"};
  for(unsigned i=0;i<10;i++){httpd_req_t r={bad[i]};CHECK(departure_buzzer_pulse_post(&r)==400);}CHECK(writes==0);
  httpd_req_t r={"61"};CHECK(departure_buzzer_pulse_post(&r)==0);CHECK(stored==61);
 }
 if(test==13){setting=1;scenario=6;httpd_req_t r={"80"};CHECK(departure_buzzer_pulse_post(&r)==500);CHECK(stored==20&&s_cfg.departure_buzzer_pulse_ms==20);}
 if(test==14){s_config_mutex=0;CHECK(app_config_set_departure_buzzer_pulse_ms(60)==5);}
 if(test==15){
  for(uint32_t width=5;width<=160;width++){
   ticks=0;off_at=0;CHECK(departure_buzzer_tone((void*)1,width,0));
   CHECK(ticks==18&&on_timeout==width*1000&&off_at==width*1000&&level==1&&!timer_active);
  }
 }
 if(test==16){ticks=0;overlap=1;CHECK(!departure_buzzer_tone((void*)1,160,0));CHECK(ticks==2&&level==1&&!timer_active);}
 if(test==17){ticks=0;timer_error=1;CHECK(!departure_buzzer_tone((void*)1,5,0));CHECK(level==1&&!timer_active);}
 if(test==18){CHECK(!departure_buzzer_tone(0,5,0));CHECK(!timer_active);}
 if(test==19){
  uint32_t width=0;s_departure_preview_ready=true;now_us=1000000;
  CHECK(app_buzzer_request_departure_preview(61)==0);
  CHECK(s_departure_preview_until_us==6000000&&departure_preview_active(&width)&&width==61);
  now_us=5999000;CHECK(departure_preview_active(&width)&&width==1);
  now_us=6000000;CHECK(!departure_preview_active(&width));CHECK(writes==0&&stored==20);
 }
 if(test==20){
  uint32_t width=0;s_departure_preview_ready=true;now_us=1000000;
  CHECK(app_buzzer_request_departure_preview(5)==0);now_us=5900000;
  CHECK(app_buzzer_request_departure_preview(160)==0);CHECK(s_departure_preview_until_us==10900000);
  now_us=6000000;CHECK(departure_preview_active(&width)&&width==160);
  now_us=10900000;CHECK(!departure_preview_active(&width));CHECK(writes==0&&s_cfg.departure_buzzer_pulse_ms==20);
 }
 if(test==21){
  s_departure_preview_ready=true;CHECK(app_buzzer_request_departure_preview(4)==4);CHECK(app_buzzer_request_departure_preview(161)==4);CHECK(s_departure_preview_until_us==0);
  s_departure_preview_ready=false;CHECK(app_buzzer_request_departure_preview(20)==5);CHECK(s_departure_preview_until_us==0);
 }
 if(test==22){
  httpd_req_t bad={"161"};CHECK(departure_buzzer_preview_post(&bad)==400);
  httpd_req_t good={"61"};CHECK(departure_buzzer_preview_post(&good)==500);
  s_departure_preview_ready=true;now_us=42;CHECK(departure_buzzer_preview_post(&good)==0);
  CHECK(s_departure_preview_pulse_ms==61&&s_departure_preview_until_us==5000042&&writes==0&&stored==20);
 }
 CHECK(locks==0);return 0;
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
    for test in range(23):
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
        print(f'PASS: departure pulse NVS/API scenario {test}')

if __name__ == '__main__':
    main()
