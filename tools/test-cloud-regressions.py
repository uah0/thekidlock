"""Реальные cloud_policy/command C-функции в RISC-V, сеть/очередь/NVS заменены стендом."""
import argparse
import importlib.util
import io
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / ".device-agent/test-deps"))
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_RISCV, UC_MODE_RISCV32
from unicorn.riscv_const import UC_RISCV_REG_SP, UC_RISCV_REG_RA, UC_RISCV_REG_A0, UC_RISCV_REG_GP, UC_RISCV_REG_PC


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", required=True)
    args = parser.parse_args()
    extract = load_module("ulp_extract", ROOT / "tools/test-ulp-regressions.py").function
    client = load_module("cloud_client", ROOT / "tools/thingspeak-control.py")
    command = client.make_command("A0:F2:62:F1:14:B8", 123, "open", "owner", now=1000)
    valid_feed = dict(field1="123", field2="ok", field3="OPEN", field4=command["field1"], field5="1001", field6="0")
    assert client.verified_result(valid_feed, command, 1000) == "ok"
    for key, bad in (("field1", "124"), ("field4", "00:00:00:00:00:00"),
                     ("field5", "900"), ("field3", "LOCKED"), ("field6", "1"), ("field2", "pending")):
        assert client.verified_result(dict(valid_feed, **{key: bad}), command, 1000) is None
    for values in (("bad", 123, "open", "owner"), (command["field1"], 0, "open", "owner"),
                   (command["field1"], 2**53, "open", "owner"), (command["field1"], 123, "toggle", "owner"),
                   (command["field1"], 123, "open", "bad&field2=9")):
        try:
            client.make_command(*values)
        except ValueError:
            pass
        else:
            raise AssertionError(values)
    print("PASS: client validation and false-result rejection")
    work = ROOT / ".device-agent/cloud-regressions"
    work.mkdir(parents=True, exist_ok=True)
    (work / "esp_err.h").write_text("#pragma once\ntypedef int esp_err_t;\n")
    source = (ROOT / "main/cloud_commands.c").read_text(encoding="utf-8")
    record_type = re.search(r"typedef struct \{.*?\} command_record_t;", source, re.S)[0]
    feedback = re.search(r"typedef struct \{.*?\} lock_feedback_t;", (ROOT / "main/lock_control.h").read_text(), re.S)[0]
    function_names = ("save_record", "finish", "accept", "check_feedback", "equal_field", "check_result", "cloud_commands_init")
    functions = "\n".join(extract(source, name) for name in function_names)
    code = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <sys/reent.h>
#include "cloud_policy.h"
static struct _reent test_reent = _REENT_INIT(test_reent);
struct _reent *__getreent(void) { return &test_reent; }
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_NVS_NOT_FOUND 4
#define ESP_MAC_WIFI_STA 0
#define NVS_READWRITE 0
#define NVS_READONLY 1
typedef int esp_err_t;
typedef int nvs_handle_t;
typedef void *QueueHandle_t;
typedef struct { bool result; char data[1025]; } message_t;
typedef enum {LOCK_STATE_STARTING,LOCK_STATE_UNLOCKED,LOCK_STATE_LOCKED,LOCK_STATE_FAULT} lock_state_t;
FEEDBACK
RECORD
static command_record_t s_record, persisted, staged;
static QueueHandle_t s_messages;
static atomic_bool s_busy;
static bool s_dirty;
static const char *s_status;
static char s_mac[18] = "a0:f2:62:f1:14:b8";
static int64_t s_dispatched_us, clock_us = 1000000;
static int64_t clock_s = 1000;
static int requests, nvs_fail, queue_fail, sync_ready = 1, has_persisted;
static lock_feedback_t feedback;
typedef struct { bool enabled, commands_enabled; uint32_t result_channel_id; } thingspeak_config_t;
static unsigned char arena[262144];
static size_t arena_used;
static void *arena_malloc(size_t n) {
    n = (n + 7) & ~7u;
    if (n > sizeof(arena) - arena_used) return NULL;
    void *p = arena + arena_used; arena_used += n; return p;
}
static void arena_free(void *p) {(void)p;}
int64_t esp_timer_get_time(void) { return clock_us; }
int64_t now_s(void) { return clock_s; }
bool cloud_link_time_ready(void) { return sync_ready; }
void cloud_link_record(const char *a,const char *b,const char *c,const char *d) {(void)a;(void)b;(void)c;(void)d;}
void event_log_add(const char *fmt,...) {(void)fmt;}
void lock_control_get_feedback(lock_feedback_t *out) {*out = feedback;}
const char *lock_control_feedback_name(const lock_feedback_t *out) {
    if (!out->valid) return "STARTING";
    if (out->fault_flags) return "FAULT";
    if (out->state == LOCK_STATE_LOCKED) return "LOCKED";
    return out->hold_open ? "OPEN_HOLD" : "OPEN";
}
int lock_request_cloud(const char *op,const char *actor,uint64_t id,int64_t deadline) {
    (void)op;(void)actor;(void)id;(void)deadline;
    /* Assert persistence happened before any physical request. */
    if (!has_persisted || !persisted.waiting || persisted.command.id != id) return -99;
    if (queue_fail) return -1;
    requests++; return ESP_OK;
}
int nvs_open(const char *ns,int mode,nvs_handle_t *h) {
    (void)ns; *h=1;
    if (nvs_fail == 1) return -1;
    return mode == NVS_READONLY && !has_persisted ? ESP_ERR_NVS_NOT_FOUND : ESP_OK;
}
int nvs_set_blob(nvs_handle_t h,const char *key,const void *value,size_t length) {
    (void)h;(void)key;(void)length;
    if (nvs_fail == 2) return -1;
    staged=*(const command_record_t*)value; return ESP_OK;
}
int nvs_commit(nvs_handle_t h) {(void)h; if(nvs_fail==3)return -1; persisted=staged; has_persisted=1;return ESP_OK;}
void nvs_close(nvs_handle_t h) {(void)h;}
int nvs_get_blob(nvs_handle_t h,const char *key,void *value,size_t *size) {
    (void)h;(void)key; *size=sizeof(persisted); *(command_record_t*)value=persisted; return ESP_OK;
}
int esp_read_mac(uint8_t *mac,int kind) {
    (void)kind; uint8_t value[]={0xa0,0xf2,0x62,0xf1,0x14,0xb8}; memcpy(mac,value,6); return ESP_OK;
}
QueueHandle_t xQueueCreate(unsigned size,unsigned item) {(void)size;(void)item;return (void*)1;}
FUNCTIONS
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int run_tests(int scenario) {
    cJSON_Hooks hooks={arena_malloc,arena_free}; cJSON_InitHooks(&hooks);
    CHECK(cloud_policy_selftest());
    thingspeak_config_t cfg={.enabled=true,.commands_enabled=true,.result_channel_id=3};
    s_record=(command_record_t){.version=1};
    feedback=(lock_feedback_t){.valid=true,.state=LOCK_STATE_LOCKED,.mode_persisted=true};
    char payload[512];
    const char *operation=scenario==9?"close":scenario==12?"hold_on":"open";
    snprintf(payload,sizeof(payload),"{\"field1\":\"%s\",\"field2\":\"123\",\"field3\":\"%s\","
             "\"field4\":\"1045\",\"field5\":\"1000\",\"field6\":\"owner\"}",s_mac,operation);
    if(scenario==4) clock_s=1045;
    if(scenario==5) clock_s=900;
    if(scenario==6) s_record.command.id=124;
    if(scenario==7) nvs_fail=3;
    if(scenario==8) feedback.fault_flags=1;
    if(scenario==9) feedback.hold_open=true;
    if(scenario==13) sync_ready=0;
    if(scenario==14) strcpy(s_mac,"00:00:00:00:00:00");
    if(scenario==15) queue_fail=1;
    if(scenario==16) {s_record.command.id=122;s_record.ack_pending=true;}
    if(scenario==20) nvs_fail=1;
    if(scenario==21) nvs_fail=2;
    accept(payload,&cfg);
    if(scenario==4||scenario==5||scenario==6||scenario==7||scenario==13||scenario==14||scenario==16||scenario==20||scenario==21) {
        CHECK(requests==0);return 0;
    }
    if(scenario==8||scenario==9||scenario==15) {
        CHECK(requests==0 && s_record.ack_pending && !s_record.waiting);
        CHECK(strcmp(s_record.result,scenario==8?"fault":scenario==9?"hold_conflict":"queue_failed")==0);
        return 0;
    }
    CHECK(requests==1 && persisted.waiting && s_record.command.id==123);
    check_feedback(); CHECK(s_record.waiting);
    if(scenario==2) {
        accept(payload,&cfg);CHECK(requests==1 && s_record.waiting);return 0;
    }
    if(scenario==18) {
        CHECK(cloud_commands_init()==ESP_OK);
        CHECK(requests==1 && !s_record.waiting && !strcmp(s_record.result,"interrupted"));
        accept(payload,&cfg);CHECK(requests==1);return 0;
    }
    clock_us=2000000;feedback.sampled_at_us=clock_us;feedback.cloud_command_id=123;
    if(scenario==10) {
        clock_us=7000000;feedback.cloud_command_id=122;check_feedback();
        CHECK(!strcmp(s_record.result,"timeout"));return 0;
    }
    if(scenario==11) {feedback.fault_flags=2;check_feedback();CHECK(!strcmp(s_record.result,"fault"));return 0;}
    if(scenario==12) {feedback.mode_persisted=false;check_feedback();CHECK(!strcmp(s_record.result,"mode_nvs"));return 0;}
    feedback.requested_open=true;feedback.state=LOCK_STATE_UNLOCKED;
    check_feedback();CHECK(!s_record.waiting && !strcmp(s_record.result,"ok") && persisted.ack_pending);
    if(scenario==3) {accept(payload,&cfg);CHECK(requests==1 && s_record.ack_pending);return 0;}
    if(scenario==17) {
        check_result("{\"field1\":\"123\",\"field2\":\"ok\",\"field3\":\"OPEN\",\"field4\":\"bad\",\"field5\":\"1000\",\"field6\":\"0\"}");
        CHECK(s_record.ack_pending);
        check_result("{\"field1\":\"123\",\"field2\":\"ok\",\"field3\":\"OPEN\",\"field4\":\"a0:f2:62:f1:14:b8\",\"field5\":\"1000\",\"field6\":\"0\"}");
        CHECK(!s_record.ack_pending && !atomic_load(&s_busy) && !persisted.ack_pending);
    }
    if(scenario==19) {
        nvs_fail=3; s_record.ack_pending=true;
        check_result("{\"field1\":\"123\",\"field2\":\"ok\",\"field3\":\"OPEN\",\"field4\":\"a0:f2:62:f1:14:b8\",\"field5\":\"1000\",\"field6\":\"0\"}");
        CHECK(s_dirty && atomic_load(&s_busy));
    }
    return 0;
}
'''.replace("FEEDBACK", feedback).replace("RECORD", record_type).replace("FUNCTIONS", functions)
    (work / "test.c").write_text(code, encoding="utf-8")
    cjson = Path("C:/esp/v5.4.4/esp-idf/components/json/cJSON")
    elf_path = work / "test.elf"
    subprocess.run([args.cc, "-march=rv32imc_zicsr_zifencei", "-mabi=ilp32", "-O1", "-nostartfiles",
                    "-I", str(work), "-I", str(ROOT / "main"), "-I", str(cjson),
                    str(work / "test.c"), str(ROOT / "main/cloud_policy.c"), str(cjson / "cJSON.c"),
                    "-Wl,-Ttext=0x10000,-e,run_tests", "-lm", "-lc", "-lgcc", "-lnosys",
                    "-o", str(elf_path)], check=True)
    elf = ELFFile(io.BytesIO(elf_path.read_bytes()))
    symbols = elf.get_section_by_name(".symtab")
    address = lambda name: symbols.get_symbol_by_name(name)[0]["st_value"]
    segments = [s for s in elf.iter_segments() if s["p_type"] == "PT_LOAD"]
    base = min(s["p_vaddr"] for s in segments) & ~4095
    end = (max(s["p_vaddr"] + s["p_memsz"] for s in segments) + 4095) & ~4095
    for scenario in range(22):
        uc = Uc(UC_ARCH_RISCV, UC_MODE_RISCV32)
        uc.mem_map(base, end - base)
        uc.mem_map(0x800000, 0x20000)
        uc.mem_map(0x900000, 0x1000)
        for segment in segments:
            uc.mem_write(segment["p_vaddr"], segment.data())
        uc.reg_write(UC_RISCV_REG_SP, 0x81fff0)
        uc.reg_write(UC_RISCV_REG_GP, address("__global_pointer$"))
        uc.reg_write(UC_RISCV_REG_RA, 0x900000)
        uc.reg_write(UC_RISCV_REG_A0, scenario)
        uc.emu_start(address("run_tests"), 0x900000, timeout=10_000_000, count=10_000_000)
        assert uc.reg_read(UC_RISCV_REG_PC) == 0x900000, f"Timeout scenario {scenario}"
        result = uc.reg_read(UC_RISCV_REG_A0)
        assert result == 0, f"Scenario {scenario}: production assertion C line {result}"
        print(f"PASS: command scenario {scenario}")


if __name__ == "__main__":
    main()
