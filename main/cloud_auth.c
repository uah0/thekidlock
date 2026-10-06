#include "cloud_auth.h"
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "cJSON.h"
#include "mbedtls/md.h"
#include "mbedtls/constant_time.h"
#include "mbedtls/platform_util.h"
static bool decimal(const char *s,uint64_t *out)
{
    if(!s||!*s||strlen(s)>16||(s[0]=='0'&&s[1])) return false;
    uint64_t n=0;
    for(;*s;s++) { if(*s<'0'||*s>'9') return false; n=n*10+(*s-'0'); }
    if(!n||n>UINT64_C(9007199254740991)) return false;
    *out=n; return true;
}
static bool hex(const char *s,size_t n)
{
    if(!s||strlen(s)!=n) return false;
    for(size_t i=0;i<n;i++) if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f'))) return false;
    return true;
}
bool cloud_auth_parse(const char *text,cloud_open_t *out)
{
    if(!text||!out||strlen(text)>384||strstr(text,"\\u0000")) return false;
    cJSON *j=cJSON_ParseWithOpts(text,NULL,true);
    const char *names[]={"phone","time","id","mac"},*values[4]={0};
    bool valid=cJSON_IsObject(j); unsigned count=0;
    cJSON *item;
    cJSON_ArrayForEach(item,j) {
        count++; bool found=false;
        for(unsigned i=0;i<4;i++) if(item->string&&!strcmp(item->string,names[i])) {
            found=true;
            if(values[i]||!cJSON_IsString(item)) valid=false;
            else values[i]=item->valuestring;
        }
        if(!found) valid=false;
    }
    cloud_open_t cmd={0}; uint64_t time=0;
    valid=valid&&count==4&&hex(values[0],13)&&values[0][0]<='1'&&hex(values[3],64)&&
          decimal(values[1],&time)&&decimal(values[2],&cmd.id);
    if(valid) { strcpy(cmd.phone,values[0]); strcpy(cmd.mac,values[3]); cmd.time=(int64_t)time; *out=cmd; }
    cJSON_Delete(j); return valid;
}
bool cloud_auth_time(int64_t issued,int64_t now,bool synced)
{
    return synced&&issued>=now-30&&issued<=now+5;
}
bool cloud_auth_sign(const uint8_t key[16],const char *text,char out[65])
{
    uint8_t tag[32];
    int rc=mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),key,16,(const unsigned char*)text,strlen(text),tag);
    if(rc==0) { const char *digits="0123456789abcdef";
        for(unsigned i=0;i<32;i++) { out[i*2]=digits[tag[i]>>4];out[i*2+1]=digits[tag[i]&15]; } out[64]=0; }
    mbedtls_platform_zeroize(tag,sizeof(tag)); return rc==0;
}
bool cloud_auth_verify(const uint8_t key[16],const char *text,const char *signature)
{
    char tag[65];
    bool ok=hex(signature,64)&&cloud_auth_sign(key,text,tag)&&mbedtls_ct_memcmp(tag,signature,64)==0;
    mbedtls_platform_zeroize(tag,sizeof(tag)); return ok;
}
bool cloud_auth_selftest(void)
{
    uint8_t key[16]; memset(key,0x0b,sizeof(key)); char tag[65];
    if(!cloud_auth_sign(key,"Hi There",tag)||strcmp(tag,"492ce020fe2534a5789dc3848806c78f4f6711397f08e7e7a12ca5a4483c8aa6")) return false;
    if(!cloud_auth_verify(key,"Hi There",tag)||cloud_auth_verify(key,"Hi There!",tag)) return false;
    if(!cloud_auth_time(1000,1000,true)||cloud_auth_time(969,1000,true)||
       cloud_auth_time(1006,1000,true)||cloud_auth_time(1000,1000,false)) return false;
    cloud_open_t out;
    const char *valid="{\"phone\":\"04c8e193a7065\",\"time\":\"1000\",\"id\":\"1\",\"mac\":\"0000000000000000000000000000000000000000000000000000000000000000\"}";
    if(!cloud_auth_parse(valid,&out)||cloud_auth_parse("{}", &out)) return false;
    const char *bad[]={
        "{\"phone\":\"04c8e193a7065\",\"time\":\"1000\",\"id\":\"01\",\"mac\":\"0000000000000000000000000000000000000000000000000000000000000000\"}",
        "{\"phone\":\"04c8e193a7065\",\"time\":\"1000\",\"id\":\"1\",\"id\":\"2\",\"mac\":\"0000000000000000000000000000000000000000000000000000000000000000\"}",
        "{\"phone\":\"04c8e193a7065\",\"time\":1000,\"id\":\"1\",\"mac\":\"0000000000000000000000000000000000000000000000000000000000000000\"}",
        "{\"phone\":\"04c8e193a7065\",\"time\":\"1000\",\"id\":\"9007199254740992\",\"mac\":\"0000000000000000000000000000000000000000000000000000000000000000\"}",
        "{\"phone\":\"24c8e193a7065\",\"time\":\"1000\",\"id\":\"1\",\"mac\":\"0000000000000000000000000000000000000000000000000000000000000000\"}",
        "[]","null","{} trailing"
    };
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) if(cloud_auth_parse(bad[i],&out)) return false;
    return true;
}
