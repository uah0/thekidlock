#include "cloud_commands.h"
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "cloud_auth.h"
#include "cloud_link.h"
#include "mqtt_config.h"
#include "wifi_ota.h"
#include "ble_phone.h"
#include "lock_control.h"
#include "event_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "mbedtls/platform_util.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"

typedef struct {
    uint32_t version,faults;
    uint64_t id;
    char phone[14];
    bool active,opened,opened_ack,final_ack;
    uint8_t phase;
} record_t;
typedef struct { int ack_id; char text[385]; } message_t;
static record_t s_record;
static const char *s_door="unknown";
static QueueHandle_t s_messages;
static esp_mqtt_client_handle_t s_client;
static atomic_bool s_connected,s_busy,s_probe,s_test_requested,s_clock_needed;
static atomic_bool s_home_ip_reconnect;
static _Atomic(const char*) s_status="disabled",s_test_status="idle";
static char s_mac[18],s_command_topic[64],s_result_topic[64];
static message_t s_fragment;
static size_t s_fragment_size;
static bool s_fragment_valid,s_dirty,s_ack_open;
static int s_ack_id=-1;
static int64_t s_published_us,s_dispatch_us;
bool cloud_commands_connected(void){return atomic_load(&s_connected);}
bool cloud_commands_busy(void){return atomic_load(&s_busy);}
const char *cloud_commands_status(void){return s_status;}
const char *cloud_commands_test_status(void){return s_test_status;}
bool cloud_commands_time_required(void){return atomic_load(&s_clock_needed);}
void cloud_commands_home_ip_ready(void)
{
    atomic_store(&s_home_ip_reconnect,true);
}

static void reconnect_after_home_ip(bool enabled,int64_t *retry)
{
    if(!atomic_load(&s_home_ip_reconnect))return;
    if(!enabled){atomic_store(&s_home_ip_reconnect,false);return;}
    *retry=0;
    if(!s_client||cloud_commands_connected()) {
        atomic_store(&s_home_ip_reconnect,false);
    } else if(esp_mqtt_client_reconnect(s_client)==ESP_OK) {
        atomic_store(&s_home_ip_reconnect,false);
        s_status="connecting";
        event_log_add("MQTT: immediate reconnect after home IP");
    }
}
static int64_t now_s(void){struct timeval t;gettimeofday(&t,NULL);return t.tv_sec;}
static esp_err_t save_record(void)
{
    nvs_handle_t h;esp_err_t err=nvs_open("mqtt_open",NVS_READWRITE,&h);
    if(err==ESP_OK){err=nvs_set_blob(h,"record",&s_record,sizeof(s_record));if(err==ESP_OK)err=nvs_commit(h);
    nvs_close(h);}
    return err;
}
static void persist(void){s_dirty=save_record()!=ESP_OK;if(s_dirty)s_status="command_nvs";}
esp_err_t cloud_commands_test_connection(void)
{
    mqtt_config_t cfg;
    if(!s_messages||cloud_commands_busy())return ESP_ERR_INVALID_STATE;
    esp_err_t err=mqtt_config_get(&cfg);
    if(err==ESP_OK && !cfg.host[0])err=ESP_ERR_INVALID_ARG;
    if(err==ESP_OK){s_test_status="checking";atomic_store(&s_test_requested,true);atomic_store(&s_clock_needed,true);}
    return err;
}
static void mqtt_event(void *arg,esp_event_base_t base,int32_t id,void *data)
{
    (void)arg;(void)base;esp_mqtt_event_handle_t e=data;
    if(id==MQTT_EVENT_CONNECTED){
        atomic_store(&s_connected,true);s_status="connected";
        event_log_add("MQTT: TCP connected");
        if(!atomic_load(&s_probe))esp_mqtt_client_subscribe(e->client,s_command_topic,1);
    }else if(id==MQTT_EVENT_SUBSCRIBED){
        if(!atomic_load(&s_probe))event_log_add("MQTT: command subscription ready");
    }else if(id==MQTT_EVENT_DISCONNECTED||id==MQTT_EVENT_ERROR){
        atomic_store(&s_connected,false);s_fragment_valid=false;
        if(id==MQTT_EVENT_ERROR&&e->error_handle){
            s_status="network_failed";
            event_log_add("MQTT: transport error socket=%d",e->error_handle->esp_transport_sock_errno);
        }else if(!strcmp(s_status,"connected"))s_status="mqtt_disconnected";
    }else if(id==MQTT_EVENT_PUBLISHED){
        message_t ack={.ack_id=e->msg_id};xQueueSend(s_messages,&ack,0);
    }else if(id==MQTT_EVENT_DATA){
        if(e->current_data_offset==0){
            s_fragment_size=0;s_fragment.ack_id=0;
            s_fragment_valid=!atomic_load(&s_probe)&&!e->retain&&e->topic&&
                e->topic_len==(int)strlen(s_command_topic)&&!memcmp(e->topic,s_command_topic,e->topic_len)&&
                e->total_data_len>0&&e->total_data_len<=384;
        }
        if(!s_fragment_valid||e->data_len<=0||e->current_data_offset!=(int)s_fragment_size||
           (size_t)e->data_len>384-s_fragment_size||memchr(e->data,0,e->data_len)){s_fragment_valid=false;return;}
        memcpy(s_fragment.text+s_fragment_size,e->data,e->data_len);s_fragment_size+=e->data_len;
        if(s_fragment_size==(size_t)e->total_data_len){
            s_fragment.text[s_fragment_size]=0;xQueueSend(s_messages,&s_fragment,0);s_fragment_valid=false;
        }
    }
}
static void accept(const char *text)
{
    cloud_open_t cmd;uint8_t key[16]={0};char canonical[192];
    if(s_record.active||s_dirty||lock_control_internet_active()||
       (s_record.id&&(!s_record.final_ack||(s_record.opened&&!s_record.opened_ack))))return;
    if(!cloud_auth_parse(text,&cmd)||!cloud_auth_time(cmd.time,now_s(),cloud_link_time_ready()))return;
    if(!ble_phone_is_owner(cmd.phone))return;
    if(ble_phone_mqtt_key(cmd.phone,key)!=ESP_OK)return;
    snprintf(canonical,sizeof(canonical),"BLELOCK1|command|%s|%s|%" PRId64 "|%" PRIu64 "|open",s_mac,cmd.phone,cmd.time,cmd.id);
    bool valid=cloud_auth_verify(key,canonical,cmd.mac);mbedtls_platform_zeroize(key,sizeof(key));
    if(!valid)return;
    nvs_handle_t h;uint64_t last=0;
    esp_err_t err=nvs_open("mqtt_open",NVS_READWRITE,&h);if(err!=ESP_OK){s_status="command_nvs";return;}
    err=nvs_get_u64(h,cmd.phone,&last);
    if((err!=ESP_OK&&err!=ESP_ERR_NVS_NOT_FOUND)||cmd.id<=last){nvs_close(h);return;}
    record_t previous=s_record;
    s_record=(record_t){.version=1,.id=cmd.id,.active=true};strcpy(s_record.phone,cmd.phone);
    err=nvs_set_u64(h,cmd.phone,cmd.id);
    if(err==ESP_OK)err=nvs_set_blob(h,"record",&s_record,sizeof(s_record));
    if(err==ESP_OK)err=nvs_commit(h);
    nvs_close(h);
    if(err!=ESP_OK){s_record=previous;s_status="command_nvs";return;}
    s_door="unknown";
    atomic_store(&s_busy,true);s_ack_id=-1;s_dispatch_us=esp_timer_get_time();
    int64_t deadline=s_dispatch_us+(cmd.time+30-now_s())*1000000;
    err=lock_request_cloud("internet_open",cmd.phone,cmd.id,deadline);
    if(err!=ESP_OK){s_record.active=false;s_record.phase=5;atomic_store(&s_busy,false);persist();}
}
static void feedback(void)
{
    if(!s_record.active)return;
    lock_feedback_t f;lock_control_get_feedback(&f);
    if(f.cloud_command_id!=s_record.id || f.internet_started_us<s_dispatch_us){
        if(esp_timer_get_time()-s_dispatch_us>10000000){
            s_record.phase=5;s_record.active=false;atomic_store(&s_busy,false);persist();
        }return;
    }
    s_door = !f.valid || f.fault_flags ? "unknown" :
        f.plate_state == PLATE_FAR ? "open" :
        f.state == LOCK_STATE_LOCKED || f.plate_state == PLATE_CONTACT ? "closed" : "unknown";
    bool changed=false;
    if(f.internet_phase>=2&&f.internet_phase<=4&&!s_record.opened){s_record.opened=true;changed=true;}
    if(f.internet_phase>=3&&f.internet_phase!=s_record.phase){
        s_record.phase=f.internet_phase;s_record.final_ack=false;changed=true;
        if(s_ack_id>=0&&!s_ack_open)s_ack_id=-1;
    }
    if(f.internet_phase>=4){s_record.active=false;atomic_store(&s_busy,false);changed=true;}
    if(s_record.faults!=f.fault_flags){s_record.faults=f.fault_flags;changed=true;}
    if(changed)persist();
}
static void puback(int id)
{
    if(id!=s_ack_id||s_dirty)return;
    if(s_ack_open)s_record.opened_ack=true;else s_record.final_ack=true;
    persist();s_ack_id=-1;
}
static void publish_result(void)
{
    if(!s_client||!cloud_commands_connected()||atomic_load(&s_probe)||s_dirty||!s_record.id||!cloud_link_time_ready())return;
    bool opened=s_record.opened&&!s_record.opened_ack;
    if(!opened&&(!s_record.phase||s_record.final_ack))return;
    int64_t now=esp_timer_get_time();
    if(s_ack_id>=0&&now-s_published_us<30000000)return;
    uint8_t key[16]={0};char canonical[224],tag[65],payload[384];
    if(ble_phone_mqtt_key(s_record.phone,key)!=ESP_OK){
        /* Revoked bonds must not receive signed replies or keep the queue blocked. */
        s_record.opened_ack=true;s_record.final_ack=true;persist();return;
    }
    const char *result=opened?"opened":s_record.phase==3?"closing":s_record.phase==4?"closed":s_record.phase==6?"interrupted":"error";
    int64_t utc=now_s();
    snprintf(canonical,sizeof(canonical),"BLELOCK1|result|%s|%s|%" PRId64 "|%" PRIu64 "|%s|%" PRIu32 "|%s",
             s_mac,s_record.phone,utc,s_record.id,result,s_record.faults,s_door);
    bool ok=cloud_auth_sign(key,canonical,tag);mbedtls_platform_zeroize(key,sizeof(key));if(!ok)return;
    snprintf(payload,sizeof(payload),"{\"phone\":\"%s\",\"time\":\"%" PRId64 "\",\"id\":\"%" PRIu64 "\",\"result\":\"%s\",\"faults\":%" PRIu32 ",\"door\":\"%s\",\"mac\":\"%s\"}",
             s_record.phone,utc,s_record.id,result,s_record.faults,s_door,tag);
    int id=esp_mqtt_client_enqueue(s_client,s_result_topic,payload,0,1,0,true);
    if(id>=0){s_ack_id=id;s_ack_open=opened;s_published_us=now;}
}
static void stop_client(void)
{
    if(s_client){esp_mqtt_client_stop(s_client);esp_mqtt_client_destroy(s_client);s_client=NULL;}
    atomic_store(&s_connected,false);s_fragment_valid=false;s_ack_id=-1;xQueueReset(s_messages);
}
static void command_task(void *arg)
{
    (void)arg;mqtt_config_t active={0},cfg;int64_t retry=0,test_until=0;
    for(;;){
        if(mqtt_config_get(&cfg)!=ESP_OK){stop_client();s_status="config_nvs";vTaskDelay(pdMS_TO_TICKS(1000));continue;}
        wifi_ota_set_mqtt_required(cfg.enabled);
        if(atomic_exchange(&s_test_requested,false)){test_until=esp_timer_get_time()+20000000;if(!cloud_commands_connected()){stop_client();retry=0;}}
        if(test_until&&cloud_commands_connected()){s_test_status="connected";test_until=0;if(!cfg.enabled)stop_client();}
        else if(test_until&&esp_timer_get_time()>=test_until){s_test_status=!strcmp(s_status,"waiting_time")?"waiting_time":!strcmp(s_status,"tls_certificate_failed")?"tls_certificate_failed":"timeout";test_until=0;}
        bool enabled=cfg.enabled||test_until;atomic_store(&s_clock_needed,enabled);
        reconnect_after_home_ip(cfg.enabled,&retry);
        if(!enabled){if(s_client)stop_client();s_status="disabled";}
        else{
            if(memcmp(&active,&cfg,sizeof(cfg))){stop_client();active=cfg;retry=0;}
            if(!cloud_link_time_ready())s_status="waiting_time";
            if(!s_client&&cloud_link_time_ready()&&esp_timer_get_time()>=retry){
                atomic_store(&s_probe,!cfg.enabled);
                snprintf(s_command_topic,sizeof(s_command_topic),"%s/command",active.prefix);
                snprintf(s_result_topic,sizeof(s_result_topic),"%s/result",active.prefix);
                esp_mqtt_client_config_t c={
                    .broker.address.hostname=active.host,.broker.address.port=MQTT_PORT,
                    .broker.address.transport=MQTT_TRANSPORT_OVER_TCP,
                    .credentials.client_id=active.client_id,
                    .session.protocol_ver=MQTT_PROTOCOL_V_3_1_1,.session.keepalive=60,
                    /* Preserve QoS1 commands across the phone/home Wi-Fi switch.
                     * HMAC timestamps and persisted IDs still bound execution. */
                    .session.disable_clean_session=cfg.enabled,
                    .network.reconnect_timeout_ms=30000,.network.timeout_ms=8000,
                    .task.stack_size=6144,.task.priority=2,.buffer.size=1024,.outbox.limit=4096
                };
                retry=esp_timer_get_time()+30000000;s_status="connecting";s_client=esp_mqtt_client_init(&c);
                if(s_client){esp_err_t err=esp_mqtt_client_register_event(s_client,ESP_EVENT_ANY_ID,mqtt_event,NULL);
                    if(err==ESP_OK)err=esp_mqtt_client_start(s_client);
                    if(err!=ESP_OK){esp_mqtt_client_destroy(s_client);s_client=NULL;}}
                if(!s_client)s_status="client_failed";
            }
        }
        if(s_dirty)s_dirty=save_record()!=ESP_OK;
        message_t m;
        while(xQueueReceive(s_messages,&m,0)==pdTRUE){if(m.ack_id)puback(m.ack_id);else if(cfg.enabled)accept(m.text);}
        feedback();if(enabled)publish_result();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
esp_err_t cloud_commands_init(void)
{
    if(!cloud_auth_selftest()){event_log_add("MQTT HMAC selftest FAIL");return ESP_FAIL;}
    event_log_add("MQTT HMAC selftest PASS");
    s_record=(record_t){.version=1};nvs_handle_t h;
    esp_err_t err=nvs_open("mqtt_open",NVS_READONLY,&h);
    if(err==ESP_OK){size_t size=sizeof(s_record);err=nvs_get_blob(h,"record",&s_record,&size);nvs_close(h);
        if(err==ESP_OK&&(size!=sizeof(s_record)||s_record.version!=1||s_record.phase>6||!memchr(s_record.phone,0,sizeof(s_record.phone))))return ESP_ERR_INVALID_STATE;}
    if(err!=ESP_OK&&err!=ESP_ERR_NVS_NOT_FOUND)return err;
    if(s_record.active){s_record.active=false;s_record.phase=6;s_record.final_ack=false;persist();}
    uint8_t mac[6];err=esp_read_mac(mac,ESP_MAC_WIFI_STA);if(err!=ESP_OK)return err;
    snprintf(s_mac,sizeof(s_mac),"%02x:%02x:%02x:%02x:%02x:%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    s_messages=xQueueCreate(8,sizeof(message_t));return s_messages?ESP_OK:ESP_ERR_NO_MEM;
}
esp_err_t cloud_commands_start(void)
{
    return s_messages&&xTaskCreate(command_task,"cloud_commands",6144,NULL,2,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
