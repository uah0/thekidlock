"""Проверка реальных C-функций журнала: сбои HTTPS/NVS, частичная доставка, время."""
import argparse
import importlib.util
import io
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / ".device-agent/test-deps"))
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_RISCV, UC_MODE_RISCV32
from unicorn.riscv_const import UC_RISCV_REG_SP, UC_RISCV_REG_RA, UC_RISCV_REG_A0, UC_RISCV_REG_GP, UC_RISCV_REG_PC


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("extract", ROOT / "tools/test-ulp-regressions.py")
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    source = (ROOT / "main/cloud_link.c").read_text(encoding="utf-8")
    types = source[source.index("typedef struct {"):source.index("static journal_t")]
    names = ("journal_save", "event_fields", "verify_delivery", "upload")
    functions = "\n".join(module.function(source, name) for name in names)
    work = ROOT / ".device-agent/cloud-journal-tests"; work.mkdir(parents=True, exist_ok=True)
    code = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stddef.h>
#include <sys/reent.h>
#include "cJSON.h"
static struct _reent test_reent = _REENT_INIT(test_reent);
struct _reent *__getreent(void) {return &test_reent;}
#define JOURNAL_SIZE 32
#define JOURNAL_VERSION 1
#define CLOUD_INTERVAL_US 16000000LL
#define ESP_OK 0
#define NVS_READWRITE 0
typedef int esp_err_t;
typedef int nvs_handle_t;
TYPES
typedef struct {bool enabled;uint32_t channel_id;char read_api_key[65],write_api_key[65];} thingspeak_config_t;
static journal_t s_journal, persisted;
static atomic_uint s_pending,s_dropped;
static const char *s_error;
static char s_mac[18]="a0:f2:62:f1:14:b8";
static uint32_t s_boot=1;
static int64_t s_anchor_us=999000000,s_last_upload,mono=1000000,utc=1000000000;
static int nvs_fail,nvs_calls,posts,scenario,time_ready=1,network=1;
static cJSON *feeds;
char debug_json[16384], debug_fields[8][64];
static unsigned char arena[1048576];
static size_t arena_used;
void *malloc(size_t n) {
    n=(n+7)&~7u;
    if(n>sizeof(arena)-arena_used)return NULL;
    void *p=arena+arena_used;arena_used+=n;return p;
}
void free(void *p) {(void)p;}
static void *json_malloc(size_t n) {return malloc(n);}
static void json_free(void *p) {(void)p;}
static int64_t utc_us(void) {return utc;}
int64_t esp_timer_get_time(void) {return mono;}
bool cloud_link_time_ready(void) {return time_ready;}
static bool network_ready(void) {return network;}
int nvs_open(const char *ns,int mode,nvs_handle_t *h) {
    (void)ns;(void)mode;*h=1;return nvs_fail==2?-1:ESP_OK;
}
int nvs_set_blob(nvs_handle_t h,const char *key,const void *value,size_t size) {
    (void)h;(void)key;(void)size;nvs_calls++;
    if(nvs_fail==1 || (scenario==7 && posts))return -1;
    memset(&persisted,0,sizeof(persisted));memcpy(&persisted,value,size);return ESP_OK;
}
int nvs_commit(nvs_handle_t h){(void)h;return ESP_OK;}
void nvs_close(nvs_handle_t h){(void)h;}
static cJSON *https_json(const char *url,const char *body,size_t capacity,int *status) {
    (void)url;(void)capacity;*status=200;
    if(body) {
        posts++;
        cJSON *request=cJSON_Parse(body),*updates=cJSON_GetObjectItemCaseSensitive(request,"updates");
        if(scenario!=2 && scenario!=5) {
            cJSON *item;unsigned i=0;
            cJSON_ArrayForEach(item,updates) {
                if(scenario==4 && posts==1 && i++!=1)continue;
                cJSON *copy=cJSON_Duplicate(item,true);
                if(scenario==6)cJSON_ReplaceItemInObject(copy,"field7",cJSON_CreateString("tampered"));
                cJSON_AddItemToArray(feeds,copy);
            }
        }
        cJSON_Delete(request);
        if(scenario==1||scenario==2||scenario==4||scenario==6)return NULL;
        if(scenario==5)return cJSON_Parse("{\"success\":\"true\"}");
        return cJSON_Parse("{\"success\":true}");
    }
    if(scenario==2)return NULL;
    cJSON *reply=cJSON_CreateObject();cJSON_AddItemToObject(reply,"feeds",cJSON_Duplicate(feeds,true));
    return reply;
}
FUNCTIONS
#define CHECK(x) do{if(!(x))return __LINE__;}while(0)
int run_tests(int test) {
    scenario=test;cJSON_Hooks hooks={json_malloc,json_free};cJSON_InitHooks(&hooks);
    feeds=cJSON_CreateArray();
    thingspeak_config_t cfg={.enabled=true,.channel_id=1};
    strcpy(cfg.read_api_key,"read");strcpy(cfg.write_api_key,"write");
    s_journal=(journal_t){.version=1,.count=test==12?32:3,.next_id=40,.channel=1};
    for(unsigned i=0;i<s_journal.count;i++) {
        s_journal.events[i]=(cloud_event_t){.id=i+1,.utc_us=utc+i,.monotonic_us=mono+i,.boot=1};
        strcpy(s_journal.events[i].event,"state");strcpy(s_journal.events[i].source,"ble");
        strcpy(s_journal.events[i].actor,"11:22:33:44:55:66");strcpy(s_journal.events[i].result,"OPEN");
    }
    if(test==3)nvs_fail=1;
    if(test==8)nvs_fail=2;
    if(test==9)time_ready=0;
    if(test==10)s_journal.delivery_s=2000;
    if(test==11){s_journal.events[0].utc_us=0;s_journal.events[0].boot=2;s_journal.events[1].utc_us=0;}
    upload(&cfg);
    char *debug=cJSON_PrintUnformatted(feeds);
    if(debug)snprintf(debug_json,sizeof(debug_json),"%s",debug);
    event_fields(&s_journal.events[0],debug_fields);
    if(test==0||test==1||test==11||test==12) {
        CHECK(posts==1 && s_journal.count==0 && persisted.count==0);
        if(test==11) {
            cJSON *a=cJSON_GetArrayItem(feeds,0),*b=cJSON_GetArrayItem(feeds,1);
            CHECK(!strcmp(cJSON_GetObjectItem(a,"field5")->valuestring,"unsynced"));
            CHECK(!strcmp(cJSON_GetObjectItem(b,"field5")->valuestring,"1000000001"));
        }
    } else if(test==2||test==5||test==6) {
        CHECK(posts==1 && s_journal.count==3);
    } else if(test==3) {
        CHECK(posts==0 && s_journal.count==3);
        nvs_fail=0;mono+=17000000;utc+=17000000;upload(&cfg);
        CHECK(posts==1 && s_journal.count==0);
    } else if(test==4) {
        CHECK(posts==1 && s_journal.count==2 && s_journal.events[0].id==1 && s_journal.events[1].id==3);
        scenario=0;mono+=17000000;utc+=17000000;upload(&cfg);
        CHECK(posts==2 && s_journal.count==0);
    } else if(test==7) {
        CHECK(posts==1 && s_journal.count==3);
        scenario=1;mono+=17000000;utc+=17000000;upload(&cfg);
        CHECK(posts==2 && s_journal.count==0);
    } else if(test==8||test==9||test==10) {CHECK(posts==0 && s_journal.count==3);}
    return 0;
}
'''.replace("TYPES", types).replace("FUNCTIONS", functions)
    (work / "test.c").write_text(code, encoding="utf-8")
    cjson = Path("C:/esp/v5.4.4/esp-idf/components/json/cJSON")
    elf_path = work / "test.elf"
    subprocess.run([args.cc,"-march=rv32imc_zicsr_zifencei","-mabi=ilp32","-O1","-nostartfiles",
                    "-I",str(cjson),str(work/"test.c"),str(cjson/"cJSON.c"),
                    "-Wl,-Ttext=0x10000,-e,run_tests","-lm","-lc","-lgcc","-lnosys","-o",str(elf_path)],check=True)
    elf=ELFFile(io.BytesIO(elf_path.read_bytes()));symbols=elf.get_section_by_name(".symtab")
    address=lambda name:symbols.get_symbol_by_name(name)[0]["st_value"]
    segments=[s for s in elf.iter_segments() if s["p_type"]=="PT_LOAD"]
    base=min(s["p_vaddr"] for s in segments)&~4095
    end=(max(s["p_vaddr"]+s["p_memsz"] for s in segments)+4095)&~4095
    for test in range(13):
        uc=Uc(UC_ARCH_RISCV,UC_MODE_RISCV32);uc.mem_map(base,end-base)
        uc.mem_map(0x800000,0x20000);uc.mem_map(0x900000,0x1000)
        for segment in segments:uc.mem_write(segment["p_vaddr"],segment.data())
        uc.reg_write(UC_RISCV_REG_SP,0x81fff0);uc.reg_write(UC_RISCV_REG_GP,address("__global_pointer$"))
        uc.reg_write(UC_RISCV_REG_RA,0x900000);uc.reg_write(UC_RISCV_REG_A0,test)
        uc.emu_start(address("run_tests"),0x900000,timeout=10_000_000,count=10_000_000)
        assert uc.reg_read(UC_RISCV_REG_PC)==0x900000,f"Timeout journal {test}"
        result=uc.reg_read(UC_RISCV_REG_A0)
        if result:
            print(bytes(uc.mem_read(address("debug_json"),16384)).split(b"\0")[0])
            print(bytes(uc.mem_read(address("debug_fields"),512)).split(b"\0"))
        assert result==0,f"Journal {test}: production assertion C line {result}"
        print(f"PASS: journal scenario {test}")


if __name__=="__main__":
    main()
