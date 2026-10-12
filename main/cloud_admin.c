#include "cloud_admin.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "ble_phone.h"
#include "cloud_auth.h"
#include "cloud_crypto.h"
#include "cloud_link.h"
#include "cJSON.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "event_log.h"
#include "mbedtls/base64.h"
#include "mbedtls/platform_util.h"
#include "nvs.h"

static ble_phone_bond_t s_bonds[BLE_PHONE_MAX_BONDS];
static size_t s_count;
static char s_token[17];
static int64_t s_expires_us;

static bool bond_is_owner(const ble_phone_bond_t *bond,const char owner_id[14])
{
    char address[13];
    const char *a=bond->address;
    int n=snprintf(address,sizeof(address),"%c%c%c%c%c%c%c%c%c%c%c%c",
        a[0],a[1],a[3],a[4],a[6],a[7],a[9],a[10],a[12],a[13],a[15],a[16]);
    if(n!=12)return false;
    for(int i=0;i<12;i++)if(address[i]>='A'&&address[i]<='F')address[i]+='a'-'A';
    return !strcmp(address,owner_id+1);
}

void cloud_admin_reset(void)
{
    s_expires_us=0;s_count=0;s_token[0]=0;
}

static bool field(const cJSON *json,const char *name,const char **value)
{
    const cJSON *item=cJSON_GetObjectItemCaseSensitive(json,name);
    if(!cJSON_IsString(item)||!item->valuestring)return false;
    *value=item->valuestring;return true;
}

static bool decimal(const char *s,uint64_t *value)
{
    if(!s||!*s||strlen(s)>16||(s[0]=='0'&&s[1]))return false;
    uint64_t n=0;
    for(;*s;s++){
        if(*s<'0'||*s>'9')return false;
        n=n*10+(*s-'0');
    }
    if(!n||n>UINT64_C(9007199254740991))return false;
    *value=n;return true;
}

static void reply(esp_mqtt_client_handle_t client,const char *topic,const char *target,
                  const char *phone,const uint8_t key[16],const char *id,
                  const char *op,const char *status,int index,int deleted)
{
    char name[68]="",payload[353],sealed[1025],addr[18]="";
    if(index>=0&&(size_t)index<s_count){
        const ble_phone_bond_t *bond=&s_bonds[index];
        size_t written=0;
        if(mbedtls_base64_encode((unsigned char*)name,sizeof(name),&written,
            (const unsigned char*)bond->name,strlen(bond->name))!=0)return;
        name[written]=0;strcpy(addr,bond->address);
    }
    struct timeval tv;gettimeofday(&tv,NULL);
    int n=snprintf(payload,sizeof(payload),
        "{\"id\":\"%s\",\"time\":\"%" PRId64 "\",\"op\":\"%s\",\"status\":\"%s\",\"count\":%u,\"index\":%d,\"token\":\"%s\",\"addr\":\"%s\",\"type\":%u,\"name\":\"%s\",\"deleted\":%d}",
        id,(int64_t)tv.tv_sec,op,status,(unsigned)s_count,index,s_token,addr,
        index>=0&&(size_t)index<s_count?(unsigned)s_bonds[index].address_type:0,
        name,deleted);
    if(n<=0||n>352||!cloud_crypto_seal(key,target,"admin-reply",phone,payload,sealed,sizeof(sealed)))return;
    /* Queue the reply in the MQTT outbox to keep command processing non-blocking. */
    int published=esp_mqtt_client_enqueue(client,topic,sealed,0,1,0,true);
    event_log_add("MQTT admin: reply %s status=%s",published>=0?"published":"failed",status);
}

void cloud_admin_handle(const char *envelope,esp_mqtt_client_handle_t client,
                        const char *target,const char *reply_topic)
{
    char phone[14],plain[385];uint8_t key[16]={0};
    if(!client||ble_phone_owner_id(phone)!=ESP_OK||!ble_phone_is_owner(phone)||
       ble_phone_mqtt_key(phone,key)!=ESP_OK){event_log_add("MQTT admin: owner key unavailable");return;}
    bool opened=cloud_crypto_open(key,target,"admin-request",phone,envelope,plain,sizeof(plain));
    if(!opened){event_log_add("MQTT admin: decrypt rejected");mbedtls_platform_zeroize(key,sizeof(key));return;}
    cJSON *json=cJSON_ParseWithOpts(plain,NULL,true);
    const char *request_phone=NULL,*time=NULL,*id=NULL,*op=NULL,*token=NULL,*mask=NULL;
    cJSON *idx=cJSON_GetObjectItemCaseSensitive(json,"index");
    bool valid=cJSON_IsObject(json)&&cJSON_GetArraySize(json)==7&&
        field(json,"phone",&request_phone)&&field(json,"time",&time)&&
        field(json,"id",&id)&&field(json,"op",&op)&&field(json,"token",&token)&&
        field(json,"mask",&mask)&&cJSON_IsNumber(idx)&&idx->valuedouble==idx->valueint&&
        !strcmp(request_phone,phone);
    uint64_t issued=0,request_id=0;
    struct timeval tv;gettimeofday(&tv,NULL);
    valid=valid&&decimal(time,&issued)&&decimal(id,&request_id)&&
        cloud_auth_time((int64_t)issued,tv.tv_sec,cloud_link_time_ready())&&
        ((!strcmp(op,"list")&&idx->valueint>=0&&idx->valueint<BLE_PHONE_MAX_BONDS&&
          strlen(token)<=16&&!*mask)||
         (!strcmp(op,"apply")&&idx->valueint==-1&&strlen(token)==16&&
          strlen(mask)<=BLE_PHONE_MAX_BONDS));
    if(!valid){event_log_add("MQTT admin: request rejected");cJSON_Delete(json);mbedtls_platform_zeroize(key,sizeof(key));return;}
    nvs_handle_t h;uint64_t last=0;
    esp_err_t err=nvs_open("mqtt_admin",NVS_READWRITE,&h);
    if(err!=ESP_OK){event_log_add("MQTT admin: NVS open failed");cJSON_Delete(json);mbedtls_platform_zeroize(key,sizeof(key));return;}
    err=nvs_get_u64(h,phone,&last);
    if((err!=ESP_OK&&err!=ESP_ERR_NVS_NOT_FOUND)||request_id<=last){event_log_add("MQTT admin: replay rejected");nvs_close(h);cJSON_Delete(json);mbedtls_platform_zeroize(key,sizeof(key));return;}
    err=nvs_set_u64(h,phone,request_id);
    if(err==ESP_OK)err=nvs_commit(h);
    nvs_close(h);
    if(err!=ESP_OK){event_log_add("MQTT admin: NVS commit failed");cJSON_Delete(json);mbedtls_platform_zeroize(key,sizeof(key));return;}
    if(!strcmp(op,"list")){
        if(idx->valueint==0&&!*token){
            ble_phone_bond_t all[BLE_PHONE_MAX_BONDS];
            size_t all_count=ble_phone_get_bonds(all,BLE_PHONE_MAX_BONDS);
            s_count=0;
            for(size_t i=0;i<all_count;i++)
                if(!bond_is_owner(&all[i],phone))s_bonds[s_count++]=all[i];
            uint8_t random[8];esp_fill_random(random,sizeof(random));
            for(unsigned i=0;i<sizeof(random);i++)snprintf(s_token+i*2,3,"%02x",random[i]);
            s_expires_us=esp_timer_get_time()+300000000;
        }
        const char *status="ok";
        if(!s_expires_us||esp_timer_get_time()>s_expires_us||
           (idx->valueint>0&&strcmp(token,s_token))||
           (s_count>0&&(size_t)idx->valueint>=s_count))status="stale";
        reply(client,reply_topic,target,phone,key,id,op,status,
              !strcmp(status,"ok")?idx->valueint:-1,0);
    }else{
        const char *status="ok";int deleted=0;
        if(!s_expires_us||esp_timer_get_time()>s_expires_us||strcmp(token,s_token)||
           strlen(mask)!=s_count)status="stale";
        ble_phone_bond_t all_current[BLE_PHONE_MAX_BONDS],current[BLE_PHONE_MAX_BONDS];
        size_t all_count=ble_phone_get_bonds(all_current,BLE_PHONE_MAX_BONDS),count=0;
        for(size_t i=0;i<all_count;i++)
            if(!bond_is_owner(&all_current[i],phone))current[count++]=all_current[i];
        if(count!=s_count)status="stale";
        if(!strcmp(status,"ok"))for(size_t i=0;i<count;i++){
            if(strcmp(current[i].address,s_bonds[i].address)||
               current[i].address_type!=s_bonds[i].address_type)status="stale";
            if(mask[i]!='0'&&mask[i]!='1')status="invalid";
            if(mask[i]=='1'){
                char candidate[14];candidate[0]=(char)('0'+current[i].address_type);
                const char *a=current[i].address;
                snprintf(candidate+1,sizeof(candidate)-1,"%c%c%c%c%c%c%c%c%c%c%c%c",
                    a[0],a[1],a[3],a[4],a[6],a[7],a[9],a[10],a[12],a[13],a[15],a[16]);
                for(int j=1;j<13;j++)if(candidate[j]>='A'&&candidate[j]<='F')candidate[j]+='a'-'A';
                char owner_id[14];
                if(ble_phone_owner_id(owner_id)!=ESP_OK||
                   !strcmp(candidate+1,owner_id+1))status="invalid";
            }
        }
        if(!strcmp(status,"ok")){
            /* Invalidate before the first irreversible bond removal. */
            s_expires_us=0;
            for(size_t i=0;i<count;i++)if(mask[i]=='1'){
                if(ble_phone_delete_bond(current[i].address)==ESP_OK)deleted++;
                else {status="partial";break;}
            }
        }
        reply(client,reply_topic,target,phone,key,id,op,status,-1,deleted);
    }
    cJSON_Delete(json);mbedtls_platform_zeroize(key,sizeof(key));
}
