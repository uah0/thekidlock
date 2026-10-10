"""Реальные cloud_auth (mbedTLS/cJSON) и accept/PUBACK в RISC-V/Unicorn.
BLE, NVS и исполнитель замка заменены стендом; криптография не заменяется.
"""
import argparse, importlib.util, io, json, re, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/".device-agent/test-deps"))
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_RISCV,UC_MODE_RISCV32
from unicorn.riscv_const import *
def main():
    ap=argparse.ArgumentParser();ap.add_argument("--cc",required=True);args=ap.parse_args()
    spec=importlib.util.spec_from_file_location("extract",ROOT/"tools/test-ulp-regressions.py")
    mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
    source=(ROOT/"main/cloud_commands.c").read_text(encoding="utf-8")
    types=re.search(r"typedef struct \{.*?\} record_t;",source,re.S)[0]
    functions="\n".join(mod.function(source,n) for n in ("cloud_commands_home_ip_ready","reconnect_after_home_ip","accept","puback","mqtt_event","publish_result"))
    functions=mod.function((ROOT/"main/ble_phone.c").read_text(encoding="utf-8"),"ble_phone_is_owner")+"\n"+functions
    work=ROOT/".device-agent/cloud-auth-tests";work.mkdir(parents=True,exist_ok=True)
    (work/"minimal.h").write_text("#define MBEDTLS_MD_C\n#define MBEDTLS_SHA256_C\n")
    code=r"""
#include <inttypes.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <reent.h>
#include "cloud_auth.h"
#include "cJSON.h"
#include "mbedtls/platform_util.h"
/* Hex formatting pulls in newlib's reentrant formatter in this bare-metal harness. */
static struct _reent test_reent = _REENT_INIT(test_reent);
struct _reent *__getreent(void){return &test_reent;}
static char test_heap[65536];
static size_t test_heap_used;
void *_sbrk(ptrdiff_t increment){
    if(increment<0||(size_t)increment>sizeof(test_heap)-test_heap_used)return (void*)-1;
    void *result=test_heap+test_heap_used;test_heap_used+=(size_t)increment;return result;
}
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND 2
#define NVS_READWRITE 1
typedef int esp_err_t;
typedef int nvs_handle_t;
TYPES
typedef struct { int ack_id; char text[385]; } message_t;
typedef const char *esp_event_base_t;
typedef struct { int esp_tls_cert_verify_flags,esp_tls_last_esp_err,esp_tls_stack_err,esp_transport_sock_errno; } fake_error;
static void event_log_add(const char *s,...){}
typedef struct { int msg_id,topic_len,total_data_len,current_data_offset,data_len,retain;
    char *topic,*data; fake_error *error_handle; void *client; } fake_event;
typedef fake_event *esp_mqtt_event_handle_t;
#define MQTT_EVENT_CONNECTED 1
#define MQTT_EVENT_DISCONNECTED 2
#define MQTT_EVENT_ERROR 3
#define MQTT_EVENT_PUBLISHED 4
#define MQTT_EVENT_DATA 5
#define MQTT_EVENT_SUBSCRIBED 6
static atomic_bool s_connected,s_probe;
static atomic_bool s_home_ip_reconnect;
static void *s_client;
static int reconnect_calls,reconnect_result;
static bool cloud_commands_connected(void){return atomic_load(&s_connected);}
static int esp_mqtt_client_reconnect(void *client){reconnect_calls++;return reconnect_result;}
static const char *s_command_topic="locks/a0f262f114b8/command";
static int s_messages;
static message_t s_fragment,queued;
static size_t s_fragment_size;
static bool s_fragment_valid;
static int queues,subscriptions,last_qos;
static int xQueueSend(int q,const message_t *m,int wait){queued=*m;queues++;return 1;}
static int esp_mqtt_client_subscribe(void *client,const char *topic,int qos){subscriptions++;last_qos=qos;return 1;}
static record_t s_record;
static const char *s_door="unknown",*s_result_topic="result";
static int64_t s_published_us;
static char published[384];
static int publish_count;
static int esp_mqtt_client_enqueue(void *client,const char *topic,const char *payload,int len,int qos,int retain,bool store){
    if(qos!=1||retain)return -1;
    strcpy(published,payload);publish_count++;return 7;
}
static atomic_bool s_busy;
static bool s_dirty,s_ack_open;
static int s_ack_id=-1;
static int64_t s_dispatch_us;
static const char *s_status;
static char s_mac[18]="a0:f2:62:f1:14:b8";
static uint64_t last_id;
static bool synced=true,has_bond=true,verify_fail;
static int nvs_fail,requests,saves;
static uint8_t key[16];
typedef struct { uint8_t type,val[6]; } ble_addr_t;
static ble_addr_t owner={0,{0x65,0x70,0x3a,0x19,0x8e,0x4c}};
static int owner_error;
static esp_err_t owner_load(ble_addr_t *out){*out=owner;return owner_error;}
static bool lock_control_internet_active(void){return false;}
static bool cloud_link_time_ready(void){return synced;}
static int64_t now_s(void){return 1000;}
static int64_t esp_timer_get_time(void){return 10000000;}
static esp_err_t ble_phone_mqtt_key(const char *phone,uint8_t out[16]){
    if(!has_bond || strcmp(phone,"04c8e193a7065"))return 1;memcpy(out,key,16);return 0;
}
static esp_err_t nvs_open(const char*n,int mode,nvs_handle_t*h){*h=1;return nvs_fail==1?1:0;}
static esp_err_t nvs_get_u64(nvs_handle_t h,const char*k,uint64_t*out){*out=last_id;return 0;}
static esp_err_t nvs_set_u64(nvs_handle_t h,const char*k,uint64_t value){last_id=value;return nvs_fail==2?1:0;}
static esp_err_t nvs_set_blob(nvs_handle_t h,const char*k,const void*b,size_t size){return nvs_fail==3?1:0;}
static esp_err_t nvs_commit(nvs_handle_t h){return nvs_fail==4?1:0;}
static void nvs_close(nvs_handle_t h){}
static esp_err_t lock_request_cloud(const char*op,const char*phone,uint64_t id,int64_t deadline){requests++;return 0;}
static void persist(void){saves++;s_dirty=nvs_fail!=0;}
FUNCTIONS
#define CHECK(x) do{if(!(x))return __LINE__;}while(0)
static void request(char out[385],uint64_t id,int64_t time,const char *target){
    char canonical[192],tag[65];
    snprintf(canonical,sizeof(canonical),"BLELOCK1|command|%s|04c8e193a7065|%" PRId64 "|%" PRIu64 "|open",target,time,id);
    cloud_auth_sign(key,canonical,tag);
    snprintf(out,385,"{\"phone\":\"04c8e193a7065\",\"time\":\"%" PRId64 "\",\"id\":\"%" PRIu64 "\",\"mac\":\"%s\"}",time,id,tag);
}
int run_tests(int test){
    if(test>=30&&test<=36){
        int64_t retry=30000000;
        s_client=(void*)1;reconnect_calls=0;reconnect_result=0;
        atomic_store(&s_connected,false);atomic_store(&s_home_ip_reconnect,false);
        if(test==30){reconnect_after_home_ip(true,&retry);CHECK(reconnect_calls==0&&retry==30000000);return 0;}
        cloud_commands_home_ip_ready();
        if(test==31){reconnect_after_home_ip(false,&retry);CHECK(reconnect_calls==0&&retry==30000000&&!atomic_load(&s_home_ip_reconnect));return 0;}
        if(test==32)s_client=NULL;
        if(test==33)atomic_store(&s_connected,true);
        if(test==34||test==35)reconnect_result=1;
        reconnect_after_home_ip(true,&retry);
        CHECK(retry==0);
        if(test==32||test==33)CHECK(reconnect_calls==0&&!atomic_load(&s_home_ip_reconnect));
        else if(test==34||test==35){
            CHECK(reconnect_calls==1&&atomic_load(&s_home_ip_reconnect));
            if(test==34){reconnect_result=0;reconnect_after_home_ip(true,&retry);CHECK(reconnect_calls==2&&!atomic_load(&s_home_ip_reconnect));}
            else {atomic_store(&s_connected,true);reconnect_after_home_ip(true,&retry);CHECK(reconnect_calls==1&&!atomic_load(&s_home_ip_reconnect));}
        } else CHECK(reconnect_calls==1&&!atomic_load(&s_home_ip_reconnect));
        return 0;
    }
    memset(key,11,16);s_record=(record_t){.version=1};s_dirty=false;last_id=0;requests=0;nvs_fail=0;has_bond=true;synced=true;
    if(test==0){CHECK(cloud_auth_selftest());return 0;}
    char payload[385];request(payload,123,1000,s_mac);
    if(test>=43){
        s_client=(void*)1;atomic_store(&s_connected,true);atomic_store(&s_probe,false);
        s_record=(record_t){.version=1,.id=123,.phase=4,.opened=true,.opened_ack=true};
        strcpy(s_record.phone,"04c8e193a7065");
        s_door=test==43?"open":test==44?"closed":"unknown";
        s_ack_id=-1;publish_count=0;
        if(test==46)has_bond=false;
        publish_result();
        if(test==46){CHECK(publish_count==0&&s_record.final_ack&&s_record.opened_ack);return 0;}
        CHECK(publish_count==1);
        cJSON *reply=cJSON_Parse(published);CHECK(reply!=NULL&&cJSON_GetArraySize(reply)==7);
        CHECK(!strcmp(cJSON_GetObjectItem(reply,"door")->valuestring,s_door));
        char canonical[224];
        snprintf(canonical,sizeof(canonical),"BLELOCK1|result|%s|04c8e193a7065|1000|123|closed|0|%s",s_mac,s_door);
        const char *tag=cJSON_GetObjectItem(reply,"mac")->valuestring;
        CHECK(cloud_auth_verify(key,canonical,tag));
        canonical[strlen(canonical)-1]^=1;CHECK(!cloud_auth_verify(key,canonical,tag));
        cJSON_Delete(reply);return 0;
    }
    if(test>=37){
        if(test==37)owner.val[0]^=1; /* Valid signature from a bonded non-owner. */
        if(test==38)owner_error=2; /* Missing owner. */
        if(test==39)owner_error=1; /* NVS read failure. */
        if(test==40)owner.type=2; /* Invalid stored identity type. */
        if(test==41){CHECK(!ble_phone_is_owner(NULL)&&!ble_phone_is_owner("bad"));return 0;}
        if(test==42){owner.type=1;CHECK(ble_phone_is_owner("14c8e193a7065"));CHECK(!ble_phone_is_owner("04c8e193a7065"));return 0;}
        accept(payload);CHECK(requests==0&&last_id==0&&!s_record.active);return 0;
    }
    if(test==2){accept(payload);s_record.active=false;s_record.opened=false;s_record.final_ack=true;requests=0;}
    if(test==3)key[0]^=1;
    if(test==4)request(payload,123,1000,"00:00:00:00:00:00");
    if(test==5){char *p=strstr(payload,"\"123\"");p[1]='2';}
    if(test==6)request(payload,123,969,s_mac);
    if(test==7)synced=false;
    if(test==8)has_bond=false;
    if(test>=9&&test<=12)nvs_fail=test-8;
    if(test==13)s_record.active=true;
    if(test==14){s_record.id=12;s_record.final_ack=false;}
    if(test==15){payload[strlen(payload)-1]=0;strcat(payload,",\"command\":\"close\"}");}
    if(test==16)request(payload,123,1006,s_mac);
    if(test==17)request(payload,123,970,s_mac);
    if(test>=21){
        fake_event e={.topic=(char*)s_command_topic,.topic_len=strlen(s_command_topic),
            .data=payload,.data_len=strlen(payload),.total_data_len=strlen(payload)};
        if(test==21)e.retain=1;
        if(test==23)e.current_data_offset=1;
        if(test==24)payload[20]=0;
        if(test==25)atomic_store(&s_probe,true);
        if(test==26){e.topic="other";e.topic_len=5;}
        if(test==27)e.total_data_len=385;
        if(test==22){
            int total=e.data_len;e.data_len=20;mqtt_event(NULL,NULL,MQTT_EVENT_DATA,&e);
            e.current_data_offset=20;e.data=payload+20;e.data_len=total-20;
        }
        if(test==28||test==29){
            if(test==29)atomic_store(&s_probe,true);
            mqtt_event(NULL,NULL,MQTT_EVENT_CONNECTED,&e);
            CHECK(subscriptions==(test==28?1:0));if(test==28)CHECK(last_qos==1);
        }else{
            mqtt_event(NULL,NULL,MQTT_EVENT_DATA,&e);
            if(test==22)CHECK(queues==1&&!strcmp(queued.text,payload));
            else CHECK(queues==0);
        }
    }else if(test<=17){
        accept(payload);
        if(test==1||test==17)CHECK(requests==1&&last_id==123&&s_record.active&&s_record.id==123);
        else CHECK(requests==0);
    }else{
        s_record.opened=true;s_record.opened_ack=false;s_ack_id=7;s_ack_open=true;
        if(test==20)s_dirty=true;
        puback(test==19?8:7);
        if(test==18)CHECK(s_record.opened_ack&&s_ack_id==-1);
        else CHECK(!s_record.opened_ack&&s_ack_id==7);
    }
    return 0;
}
""".replace("TYPES",types).replace("FUNCTIONS",functions)
    (work/"test.c").write_text(code,encoding="utf-8")
    mbed=Path("C:/esp/v5.4.4/esp-idf/components/mbedtls/mbedtls")
    cjson=Path("C:/esp/v5.4.4/esp-idf/components/json/cJSON")
    elf_path=work/"test.elf"
    cmd=[args.cc,"-march=rv32imc_zicsr_zifencei","-mabi=ilp32","-O1","-nostartfiles",
        '-DMBEDTLS_CONFIG_FILE="minimal.h"',"-I",str(work),"-I",str(ROOT/"main"),"-I",str(mbed/"include"),
        "-I",str(mbed/"library"),"-I",str(cjson),str(work/"test.c"),str(ROOT/"main/cloud_auth.c"),str(cjson/"cJSON.c")]
    cmd += [str(mbed/"library"/n) for n in ("md.c","sha256.c","platform_util.c","constant_time.c")]
    cmd+=["-Wl,-Ttext=0x10000,-e,run_tests","-lm","-lc","-lgcc","-lnosys","-o",str(elf_path)]
    subprocess.run(cmd,check=True)
    elf=ELFFile(io.BytesIO(elf_path.read_bytes()));syms=elf.get_section_by_name(".symtab")
    addr=lambda n:syms.get_symbol_by_name(n)[0]["st_value"]
    segments=[s for s in elf.iter_segments() if s["p_type"]=="PT_LOAD"]
    base=min(s["p_vaddr"] for s in segments)&~4095
    end=(max(s["p_vaddr"]+s["p_memsz"] for s in segments)+4095)&~4095
    for test in range(47):
        uc=Uc(UC_ARCH_RISCV,UC_MODE_RISCV32);uc.mem_map(base,end-base)
        uc.mem_map(0x800000,0x20000);uc.mem_map(0x900000,0x1000)
        for seg in segments:uc.mem_write(seg["p_vaddr"],seg.data())
        uc.reg_write(UC_RISCV_REG_SP,0x81fff0);uc.reg_write(UC_RISCV_REG_GP,addr("__global_pointer$"))
        uc.reg_write(UC_RISCV_REG_RA,0x900000);uc.reg_write(UC_RISCV_REG_A0,test)
        try:
            uc.emu_start(addr("run_tests"),0x900000,timeout=10_000_000,count=10_000_000)
        except Exception:
            print(f"Scenario {test}, PC={uc.reg_read(UC_RISCV_REG_PC):#x}",flush=True)
            raise
        assert uc.reg_read(UC_RISCV_REG_PC)==0x900000,"Timeout"
        result=uc.reg_read(UC_RISCV_REG_A0)
        assert result==0,f"Scenario {test}: assertion C line {result}"
        print(f"PASS: production HMAC/NVS scenario {test}")
if __name__=="__main__":main()
